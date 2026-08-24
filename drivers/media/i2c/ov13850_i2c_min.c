#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <linux/property.h>
#include <linux/videodev2.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>

/*
 * 学习驱动在整条摄像头链路中的职责：
 *
 *   用户态 / RKISP notifier
 *            |
 *            v
 *   V4L2 sensor subdev（本文件）
 *            |  通过 I2C 配置分辨率、曝光、增益、VTS 和 stream 状态
 *            v
 *   OV13850 -> MIPI CSI-2 RAW10 -> D-PHY/CIF -> RKISP
 *
 * 本驱动只管理 sensor，不接收图像帧，也不负责 ISP/RGA 处理。它向 media
 * graph 描述 OV13850 能输出什么格式，并在上游请求 stream-on 时保证电源、
 * 时钟、全局寄存器、模式寄存器和 controls 按正确顺序落到芯片。
 *
 * 为避免与正式 ov13850.c 抢占同一设备，本学习驱动只匹配
 * "learning,ov13850-i2c"。
 */


#define OV13850_XVCLK_FREQ      24000000    // 外部输入时钟频率24MHz
#define OV13850_CHIP_ID_REG     0x300a      // OV13850芯片ID寄存器地址
#define OV13850_REVISION_REG    0x302a      // OV13850芯片版本寄存器地址     
#define OV13850_CHIP_ID         0xd850      // OV13850芯片ID值, 读取OV13850_CHIP_ID_REG的值应为0xd850
#define OV13850_REG_END         0xfffe		// 寄存器结束标志
#define OV13850_REG_DELAY       0xffff		// 寄存器间隔
#define OV13850_LINK_FREQ_300MHZ        300000000ULL
#define OV13850_LANES                   2
#define OV13850_BITS_PER_SAMPLE         10
#define OV13850_PIXEL_RATE              \
	(OV13850_LINK_FREQ_300MHZ * 2 * OV13850_LANES / OV13850_BITS_PER_SAMPLE)

#define OV13850_VTS_MAX          0x7fff

#define OV13850_REG_EXPOSURE     0x3500
#define OV13850_EXPOSURE_MIN     2
#define OV13850_EXPOSURE_STEP    1

#define OV13850_REG_GAIN_H       0x350a
#define OV13850_REG_GAIN_L       0x350b
#define OV13850_GAIN_H_MASK      0x07
#define OV13850_GAIN_H_SHIFT     8
#define OV13850_GAIN_L_MASK      0xff
#define OV13850_GAIN_MIN         0x10
#define OV13850_GAIN_MAX         0xf8
#define OV13850_GAIN_STEP        1
#define OV13850_GAIN_DEFAULT     0x10

#define OV13850_REG_TEST_PATTERN         0x5e00
#define OV13850_TEST_PATTERN_ENABLE      0x80
#define OV13850_TEST_PATTERN_DISABLE     0x00

#define OV13850_REG_VTS          0x380e
#define OV13850_R2A              0xb2
#define OV13850_PAD_SOURCE      0
#define OV13850_NUM_PADS        1

#define OV13850_MBUS_CODE       MEDIA_BUS_FMT_SBGGR10_1X10

#define OV13850_NUM_SUPPLIES    3           // OV13850芯片电源数量   

static const char * const ov13850_supply_names[] = {
	"avdd",     // 模拟电源
	"dovdd",    // IO电源
	"dvdd",     // 数字核心电源
};

static const s64 ov13850_link_freq_menu_items[] = {
	OV13850_LINK_FREQ_300MHZ,
};

static const char * const ov13850_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4",
};

struct ov13850_regval {
	u16 reg;
	u8 val;
};

/*
 * 一个 mode 是“用户可枚举的视频模式”和“芯片寄存器配置”的连接点。
 * width/height/max_fps 面向 V4L2；HTS/VTS/exp_def 用于时序和 control 范围；
 * reg_list 则是在真正 stream-on 前写入 sensor 的模式寄存器表。
 */
struct ov13850_mode {
	const char *name;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u64 pixel_rate;
	const struct ov13850_regval *reg_list;
};

/*
 * 每个 I2C sensor 实例对应一个 ov13850_min。
 *
 * 资源层：client、clock、GPIO、regulator 和 powered；
 * 运行层：cur_mode、global_regs、streaming 和 controls；
 * V4L2 层：subdev、source pad 和 ACTIVE format。
 *
 * lock 把格式切换、control 更新、stream 状态和调试 sysfs 串行化，防止它们
 * 同时改变同一组芯片寄存器或软件状态。
 */
struct ov13850_min {
	struct i2c_client *client;
	struct clk *xvclk;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	struct regulator_bulk_data supplies[OV13850_NUM_SUPPLIES];
	bool powered;
	bool streaming;

	struct mutex lock;	// 保护 sysfs 读写过程

	u16 current_reg; // 当前准备读写的寄存器
	const struct ov13850_mode *cur_mode;
	const struct ov13850_regval *global_regs; // 当前使用的全局寄存器配置表

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *anal_gain;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *test_pattern;

	struct v4l2_subdev sd;			// 代表V4L2 的sub-device	
	struct media_pad pad; 			// media graph 中的 source pad
	struct v4l2_mbus_framefmt fmt;	// 保存当前的pad format
};

// 根据subdev获取ov13850_min结构体指针
static inline struct ov13850_min *to_ov13850_min(struct v4l2_subdev *sd)
{	
	// container_of宏用于获取包含某个成员的结构体指针，
	// 这里是通过sd成员获取ov13850_min结构体指针
	return container_of(sd, struct ov13850_min, sd);
}

static inline struct ov13850_min *
ov13850_min_from_client(struct i2c_client *client)
{
	/*
	 * v4l2_i2c_subdev_init() 把 clientdata 保存为 struct v4l2_subdev *，
	 * 不是 struct ov13850_min *。先取回 sd，再用 container_of 找到私有
	 * 结构，避免把不同类型的指针直接转换后访问错误内存。
	 */
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	return to_ov13850_min(sd);
}

/*
 * Xclk 24Mhz
 */
static const struct ov13850_regval ov13850_global_regs_r1a[] = {
	{0x0103, 0x01},
	{0x0300, 0x00},
	{0x0301, 0x00},
	{0x0302, 0x32},
	{0x0303, 0x01},
	{0x030a, 0x00},
	{0x300f, 0x11},
	{0x3010, 0x01},
	{0x3011, 0x76},
	{0x3012, 0x21},
	{0x3013, 0x12},
	{0x3014, 0x11},
	{0x3015, 0xc0},
	{0x301f, 0x03},
	{0x3106, 0x00},
	{0x3210, 0x47},
	{0x3500, 0x00},
	{0x3501, 0x60},
	{0x3502, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x02},
	{0x3508, 0x00},
	{0x350a, 0x00},
	{0x350b, 0x80},
	{0x350e, 0x00},
	{0x350f, 0x10},
	{0x3600, 0x40},
	{0x3601, 0xfc},
	{0x3602, 0x02},
	{0x3603, 0x48},
	{0x3604, 0xa5},
	{0x3605, 0x9f},
	{0x3607, 0x00},
	{0x360a, 0x40},
	{0x360b, 0x91},
	{0x360c, 0x49},
	{0x360f, 0x8a},
	{0x3611, 0x10},
	{0x3612, 0x27},
	{0x3613, 0x33},
	{0x3615, 0x08},
	{0x3641, 0x02},
	{0x3660, 0x82},
	{0x3668, 0x54},
	{0x3669, 0x40},
	{0x3667, 0xa0},
	{0x3702, 0x40},
	{0x3703, 0x44},
	{0x3704, 0x2c},
	{0x3705, 0x24},
	{0x3706, 0x50},
	{0x3707, 0x44},
	{0x3708, 0x3c},
	{0x3709, 0x1f},
	{0x370a, 0x26},
	{0x370b, 0x3c},
	{0x3720, 0x66},
	{0x3722, 0x84},
	{0x3728, 0x40},
	{0x372a, 0x00},
	{0x372f, 0x90},
	{0x3710, 0x28},
	{0x3716, 0x03},
	{0x3718, 0x10},
	{0x3719, 0x08},
	{0x371c, 0xfc},
	{0x3760, 0x13},
	{0x3761, 0x34},
	{0x3767, 0x24},
	{0x3768, 0x06},
	{0x3769, 0x45},
	{0x376c, 0x23},
	{0x3d84, 0x00},
	{0x3d85, 0x17},
	{0x3d8c, 0x73},
	{0x3d8d, 0xbf},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x10},
	{0x3805, 0x97},
	{0x3806, 0x0c},
	{0x3807, 0x4b},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x12},
	{0x380d, 0xc0},
	{0x380e, 0x06},
	{0x380f, 0x80},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x02},
	{0x3821, 0x05},
	{0x3834, 0x00},
	{0x3835, 0x1c},
	{0x3836, 0x08},
	{0x3837, 0x02},
	{0x4000, 0xf1},
	{0x4001, 0x00},
	{0x400b, 0x0c},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x4020, 0x00},
	{0x4021, 0xE4},
	{0x4022, 0x07},
	{0x4023, 0x5F},
	{0x4024, 0x08},
	{0x4025, 0x44},
	{0x4026, 0x08},
	{0x4027, 0x47},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x04},
	{0x402b, 0x08},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0c},
	{0x402f, 0x08},
	{0x403d, 0x2c},
	{0x403f, 0x7f},
	{0x4500, 0x82},
	{0x4501, 0x38},
	{0x4601, 0x04},
	{0x4602, 0x22},
	{0x4603, 0x01},
	{0x4800, 0x24}, //MIPI CLK control
	{0x4837, 0x1b},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x90},
	{0x4d04, 0x66},
	{0x4d05, 0x65},
	{0x5000, 0x0e},
	{0x5001, 0x01},
	{0x5002, 0x07},
	{0x5013, 0x40},
	{0x501c, 0x00},
	{0x501d, 0x10},
	{0x5242, 0x00},
	{0x5243, 0xb8},
	{0x5244, 0x00},
	{0x5245, 0xf9},
	{0x5246, 0x00},
	{0x5247, 0xf6},
	{0x5248, 0x00},
	{0x5249, 0xa6},
	{0x5300, 0xfc},
	{0x5301, 0xdf},
	{0x5302, 0x3f},
	{0x5303, 0x08},
	{0x5304, 0x0c},
	{0x5305, 0x10},
	{0x5306, 0x20},
	{0x5307, 0x40},
	{0x5308, 0x08},
	{0x5309, 0x08},
	{0x530a, 0x02},
	{0x530b, 0x01},
	{0x530c, 0x01},
	{0x530d, 0x0c},
	{0x530e, 0x02},
	{0x530f, 0x01},
	{0x5310, 0x01},
	{0x5400, 0x00},
	{0x5401, 0x61},
	{0x5402, 0x00},
	{0x5403, 0x00},
	{0x5404, 0x00},
	{0x5405, 0x40},
	{0x540c, 0x05},
	{0x5b00, 0x00},
	{0x5b01, 0x00},
	{0x5b02, 0x01},
	{0x5b03, 0xff},
	{0x5b04, 0x02},
	{0x5b05, 0x6c},
	{0x5b09, 0x02},
	{0x5e00, 0x00},
	{0x5e10, 0x1c},
	{0x0102, 0x01}, //Fast standby enable
	{OV13850_REG_END, 0x00},
};

/*
 * Xclk 24Mhz
 */
static const struct ov13850_regval ov13850_global_regs_r2a[] = {
	{0x0300, 0x01},
	{0x0301, 0x00},
	{0x0302, 0x28},
	{0x0303, 0x00},
	{0x030a, 0x00},
	{0x300f, 0x11},
	{0x3010, 0x01},
	{0x3011, 0x76},
	{0x3012, 0x21},
	{0x3013, 0x12},
	{0x3014, 0x11},
	{0x301f, 0x03},
	{0x3106, 0x00},
	{0x3210, 0x47},
	{0x3500, 0x00},
	{0x3501, 0x60},
	{0x3502, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x02},
	{0x3508, 0x00},
	{0x350a, 0x00},
	{0x350b, 0x80},
	{0x350e, 0x00},
	{0x350f, 0x10},
	{0x351a, 0x00},
	{0x351b, 0x10},
	{0x351c, 0x00},
	{0x351d, 0x20},
	{0x351e, 0x00},
	{0x351f, 0x40},
	{0x3520, 0x00},
	{0x3521, 0x80},
	{0x3600, 0xc0},
	{0x3601, 0xfc},
	{0x3602, 0x02},
	{0x3603, 0x78},
	{0x3604, 0xb1},
	{0x3605, 0xb5},
	{0x3606, 0x73},
	{0x3607, 0x07},
	{0x3609, 0x40},
	{0x360a, 0x30},
	{0x360b, 0x91},
	{0x360c, 0x09},
	{0x360f, 0x02},
	{0x3611, 0x10},
	{0x3612, 0x27},
	{0x3613, 0x33},
	{0x3615, 0x0c},
	{0x3616, 0x0e},
	{0x3641, 0x02},
	{0x3660, 0x82},
	{0x3668, 0x54},
	{0x3669, 0x00},
	{0x366a, 0x3f},
	{0x3667, 0xa0},
	{0x3702, 0x40},
	{0x3703, 0x44},
	{0x3704, 0x2c},
	{0x3705, 0x01},
	{0x3706, 0x15},
	{0x3707, 0x44},
	{0x3708, 0x3c},
	{0x3709, 0x1f},
	{0x370a, 0x27},
	{0x370b, 0x3c},
	{0x3720, 0x55},
	{0x3722, 0x84},
	{0x3728, 0x40},
	{0x372a, 0x00},
	{0x372b, 0x02},
	{0x372e, 0x22},
	{0x372f, 0x90},
	{0x3730, 0x00},
	{0x3731, 0x00},
	{0x3732, 0x00},
	{0x3733, 0x00},
	{0x3710, 0x28},
	{0x3716, 0x03},
	{0x3718, 0x10},
	{0x3719, 0x0c},
	{0x371a, 0x08},
	{0x371c, 0xfc},
	{0x3748, 0x00},
	{0x3760, 0x13},
	{0x3761, 0x33},
	{0x3762, 0x86},
	{0x3763, 0x16},
	{0x3767, 0x24},
	{0x3768, 0x06},
	{0x3769, 0x45},
	{0x376c, 0x23},
	{0x376f, 0x80},
	{0x3773, 0x06},
	{0x3d84, 0x00},
	{0x3d85, 0x17},
	{0x3d8c, 0x73},
	{0x3d8d, 0xbf},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x10},
	{0x3805, 0x97},
	{0x3806, 0x0c},
	{0x3807, 0x4b},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x12},
	{0x380d, 0xc0},
	{0x380e, 0x06},
	{0x380f, 0x80},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x02},
	{0x3821, 0x06},
	{0x3823, 0x00},
	{0x3826, 0x00},
	{0x3827, 0x02},
	{0x3834, 0x00},
	{0x3835, 0x1c},
	{0x3836, 0x08},
	{0x3837, 0x02},
	{0x4000, 0xf1},
	{0x4001, 0x00},
	{0x4006, 0x04},
	{0x4007, 0x04},
	{0x400b, 0x0c},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x4020, 0x00},
	{0x4021, 0xe4},
	{0x4022, 0x04},
	{0x4023, 0xd7},
	{0x4024, 0x05},
	{0x4025, 0xbc},
	{0x4026, 0x05},
	{0x4027, 0xbf},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x04},
	{0x402b, 0x08},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0c},
	{0x402f, 0x08},
	{0x403d, 0x2c},
	{0x403f, 0x7f},
	{0x4041, 0x07},
	{0x4500, 0x82},
	{0x4501, 0x3c},
	{0x458b, 0x00},
	{0x459c, 0x00},
	{0x459d, 0x00},
	{0x459e, 0x00},
	{0x4601, 0x83},
	{0x4602, 0x22},
	{0x4603, 0x01},
	{0x4800, 0x24}, //MIPI CLK control
	{0x4837, 0x19},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x90},
	{0x4d04, 0x66},
	{0x4d05, 0x65},
	{0x4d0b, 0x00},
	{0x5000, 0x0e},
	{0x5001, 0x01},
	{0x5002, 0x07},
	{0x5013, 0x40},
	{0x501c, 0x00},
	{0x501d, 0x10},
	{0x510f, 0xfc},
	{0x5110, 0xf0},
	{0x5111, 0x10},
	{0x536d, 0x02},
	{0x536e, 0x67},
	{0x536f, 0x01},
	{0x5370, 0x4c},
	{0x5400, 0x00},
	{0x5400, 0x00},
	{0x5401, 0x61},
	{0x5402, 0x00},
	{0x5403, 0x00},
	{0x5404, 0x00},
	{0x5405, 0x40},
	{0x540c, 0x05},
	{0x5501, 0x00},
	{0x5b00, 0x00},
	{0x5b01, 0x00},
	{0x5b02, 0x01},
	{0x5b03, 0xff},
	{0x5b04, 0x02},
	{0x5b05, 0x6c},
	{0x5b09, 0x02},
	{0x5e00, 0x00},
	{0x5e10, 0x1c},
	{0x0102, 0x01}, //Fast standby enable
	{OV13850_REG_END, 0x00},
};


/*
 * Xclk 24Mhz
 * max_framerate 7fps
 * mipi_datarate per lane 600Mbps
 */
static const struct ov13850_regval  ov13850_4224x3136_regs[] = {
	{0x3612, 0x2f},
	{0x370a, 0x24},
	{0x372a, 0x04},
	{0x372f, 0xa0},
	{0x3801, 0x0C},
	{0x3805, 0x93},
	{0x3807, 0x4B},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380e, 0x0d},
	{0x380f, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x04},
	{0x3836, 0x04},
	{0x3837, 0x01},
	{0x4601, 0x87},
	{0x4603, 0x01},
	{0x4020, 0x02},
	{0x4021, 0x4C},
	{0x4022, 0x0E},
	{0x4023, 0x37},
	{0x4024, 0x0F},
	{0x4025, 0x1C},
	{0x4026, 0x0F},
	{0x4027, 0x1F},
	{0x4603, 0x00},
	{0x5401, 0x71},
	{0x5405, 0x80},
	{OV13850_REG_END, 0x00},
};


static const struct ov13850_regval ov13850_safe_stop_regs[] = {
	{ 0x0100, 0x00 },
	{ OV13850_REG_DELAY, 5 },
	{ OV13850_REG_END, 0x00 },
};
static const struct ov13850_regval ov13850_2112x1568_regs[] = {
	{ 0x3612, 0x27 },
	{ 0x370a, 0x26 },
	{ 0x372a, 0x00 },
	{ 0x372f, 0x90 },

	{ 0x3801, 0x08 },
	{ 0x3805, 0x97 },
	{ 0x3807, 0x4b },

	{ 0x3808, 0x08 },
	{ 0x3809, 0x40 },
	{ 0x380a, 0x06 },
	{ 0x380b, 0x20 },

	{ 0x380c, 0x12 },
	{ 0x380d, 0xc0 },
	{ 0x380e, 0x06 },
	{ 0x380f, 0x80 },

	{ 0x3813, 0x02 },
	{ 0x3814, 0x31 },
	{ 0x3815, 0x31 },
	{ 0x3820, 0x02 },
	{ 0x3821, 0x05 },
	{ 0x3836, 0x08 },
	{ 0x3837, 0x02 },

	{ 0x4601, 0x04 },
	{ 0x4603, 0x00 },

	{ 0x4020, 0x00 },
	{ 0x4021, 0xe4 },
	{ 0x4022, 0x07 },
	{ 0x4023, 0x5f },
	{ 0x4024, 0x08 },
	{ 0x4025, 0x44 },
	{ 0x4026, 0x08 },
	{ 0x4027, 0x47 },

	{ 0x4603, 0x01 },
	{ 0x5401, 0x61 },
	{ 0x5405, 0x40 },

	/*
	 * Keep sensor in software standby.
	 * Do not stream on in stage 5B.
	 */
	{ 0x0100, 0x00 },
	{ OV13850_REG_DELAY, 5 },
	{ OV13850_REG_END, 0x00 },
};


static const struct ov13850_mode ov13850_safe_mode = {
	.name = "safe_stop_only",
	.width = 2112,
	.height = 1568,
	.hts_def = 0,
	.vts_def = 0,
	.exp_def = 0,
	.link_freq_idx = 0,
	.pixel_rate = 0,
	.reg_list = ov13850_safe_stop_regs,
};
static const struct ov13850_mode ov13850_2112x1568_mode = {
	.name = "2112x1568_30fps_mode_only",
	.width = 2112,
	.height = 1568,
	.max_fps = {
		.numerator = 10000,
		.denominator = 300000,
	},
	.hts_def = 0x12c0,
	.vts_def = 0x0680,
	.exp_def = 0x0600,
	.link_freq_idx = 0,
	.pixel_rate = OV13850_PIXEL_RATE,
	.reg_list = ov13850_2112x1568_regs,
};

static const struct ov13850_mode ov13850_4224x3136_mode = {
    .name = "4224x3136_7_5fps_mode_only",
    .width = 4224,
    .height = 3136,
    .max_fps = {
        .numerator = 10000,
        .denominator = 75000,
    },
    .hts_def = 0x12c0,
    .vts_def = 0x0d00,
    .exp_def = 0x0600,
    .link_freq_idx = 0,
    .pixel_rate = OV13850_PIXEL_RATE,
    .reg_list = ov13850_4224x3136_regs,
};

static const struct ov13850_mode * const ov13850_min_supported_modes[] = {
    &ov13850_2112x1568_mode,
    &ov13850_4224x3136_mode,
};

static int ov13850_min_read_reg(struct i2c_client *client, u16 reg, 
                                unsigned int len, u32 *val)
{
    struct i2c_msg msgs[2];
    __be16 reg_be = cpu_to_be16(reg); // 保证寄存器地址按照内核的大端格式传输
    __be32 data_be = 0;
    u8 *data = (u8 *)&data_be;
    int ret;

    if(!len || len > 4)
        return -EINVAL;

    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = (u8 *)&reg_be;


	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data[4 - len];

    ret = i2c_transfer(client->adapter, msgs,  ARRAY_SIZE(msgs));
    if(ret != ARRAY_SIZE(msgs)) {
        // 这里不用打印错误信息吗？
		// 不用，这里就是用错误码来代替错误信息
		return ret < 0 ? ret : -EIO;
    }


	*val = be32_to_cpu(data_be);    // 将内核的大端格式转换为CPU的本地字节序
    return 0;
}

static int ov13850_min_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret < 0 ? ret : -EIO;

	return 0;
}
static int ov13850_min_write_reg16(struct i2c_client *client,
				   u16 reg, u16 val)
{
	int ret;

	ret = ov13850_min_write_reg(client, reg, val >> 8);
	if (ret)
		return ret;

	return ov13850_min_write_reg(client, reg + 1, val & 0xff);
}

static int ov13850_min_write_reg24(struct i2c_client *client,
				   u16 reg, u32 val)
{
	int ret;

	if (val > 0xffffff)
		return -EINVAL;

	ret = ov13850_min_write_reg(client, reg, (val >> 16) & 0xff);
	if (ret)
		return ret;

	ret = ov13850_min_write_reg(client, reg + 1, (val >> 8) & 0xff);
	if (ret)
		return ret;

	return ov13850_min_write_reg(client, reg + 2, val & 0xff);
}

static int ov13850_min_write_array(struct i2c_client *client,
				   const struct ov13850_regval *regs)
{
	int i;
	int ret;

	if (!regs)
		return -EINVAL;

	for (i = 0; regs[i].reg != OV13850_REG_END; i++) {
		if (regs[i].reg == OV13850_REG_DELAY) {
			dev_info(&client->dev,
				"array delay: index=%d delay=%u ms\n",
				i, regs[i].val);
			msleep(regs[i].val);
			continue;
		}
		if (regs[i].reg >= 0x3808 && regs[i].reg <= 0x380f)
			dev_info(&client->dev,
				"mode key write: index=%d reg=0x%04x val=0x%02x\n",
				i, regs[i].reg, regs[i].val);
		ret = ov13850_min_write_reg(client, regs[i].reg, regs[i].val);
		if (ret) {
			dev_err(&client->dev,
				"array write failed: index=%d reg=0x%04x val=0x%02x ret=%d\n",
				i, regs[i].reg, regs[i].val, ret);
			return ret;
		}
	}

	dev_info(&client->dev, "array write done, %d items\n", i);

	return 0;
}

/*
 * 把当前 ACTIVE mode 对应的寄存器表写入 sensor。set_fmt() 只选择 mode 并
 * 更新软件状态，不立即访问硬件；真正的模式切换统一放在 stream-on 路径，
 * 这样断电状态下的格式协商不会产生 I2C 访问。
 */
static int ov13850_min_apply_mode(struct ov13850_min *cam)
{
	struct i2c_client *client = cam->client;
	const struct ov13850_mode *mode = cam->cur_mode;
	int ret;

	if (!mode)
		return -EINVAL;

	if (!mode->reg_list)
		return -EINVAL;

	dev_info(&client->dev, "applying mode: %s %ux%u\n", 
							mode->name, mode->width, mode->height);
	ret = ov13850_min_write_array(client, mode->reg_list);
	if (ret) {
		dev_err(&client->dev,
			"failed to apply mode %s: %d\n",
			mode->name, ret);
		return ret;
	}
	
	dev_info(&client->dev, "mode applied: %s\n", mode->name);

	return 0;
}

/*
 * 芯片 revision 决定全局模拟/数字链路初始化表。当前板端实测为 R2A
 * (0xb2)，学习驱动主动拒绝未知 revision，避免“ID 相同但寄存器语义不同”
 * 时静默写入错误表。
 */
static int ov13850_min_select_global_regs(struct ov13850_min *cam)
{
	struct i2c_client *client = cam->client;
	u32 revision = 0;
	int ret;

	ret = ov13850_min_read_reg(client, OV13850_REVISION_REG, 1, &revision);
	if (ret) {
		dev_err(&client->dev, "failed to read revision for global regs: %d\n", ret);
		return ret;
	}

	if (revision == OV13850_R2A) {
		cam->global_regs = ov13850_global_regs_r2a;
		dev_info(&client->dev, "select global regs: R2A revision=0x%02x\n",
			 revision);
	} else {
		/*
		 * 当前学习驱动只接 R2A。
		 * 你的板子实测 revision=0xb2，所以这里暂不接 R1A。
		 */
		dev_err(&client->dev,
			"unsupported revision 0x%02x in stage 5C\n", revision);
		return -ENODEV;
	}

	return 0;
}

static int ov13850_min_apply_global_init(struct ov13850_min *cam)
{
	struct i2c_client *client = cam->client;
	int ret;

	if (!cam->global_regs)
		return -EINVAL;

	dev_info(&client->dev, "apply global init regs\n");

	ret = ov13850_min_write_array(client, cam->global_regs);
	if (ret) {
		dev_err(&client->dev, "failed to apply global init regs: %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "global init regs applied\n");

	return 0;
}

/*
 * 完整初始化用于调试入口：先建立芯片公共基线，再应用当前模式，最后保持
 * 0x0100=0 的 software standby。初始化寄存器不等于开始出图，真正 stream-on
 * 只能由 V4L2 s_stream() 控制。
 */
static int ov13850_min_apply_full_init(struct ov13850_min *cam)
{
	struct i2c_client *client = cam->client;
	int ret;

	dev_info(&client->dev, "apply full init: global + mode\n");

	ret = ov13850_min_apply_global_init(cam);
	if (ret)
		return ret;

	ret = ov13850_min_apply_mode(cam);
	if (ret)
		return ret;

	/*
	 * Keep sensor in software standby in stage 5C.
	 * Do not stream on here.
	 */
	ret = ov13850_min_write_reg(client, 0x0100, 0x00); // 为什么选择写入这个寄存器和值
	if (ret) {
		dev_err(&client->dev, "failed to force stream off: %d\n", ret);
		return ret;
	}

	dev_info(&client->dev, "full init applied, stream remains off\n");

	return 0;
}


static int ov13850_min_power_on(struct ov13850_min *cam)
{
    struct device *dev = &cam->client->dev;
    int ret;

    /*
     * 业务顺序是模块电源 -> 24 MHz 时钟 -> regulators/reset -> 退出 PWDN
     * -> 等待首个 SCCB/I2C 事务。powered 让重复 resume 保持幂等。
     */
    if(cam->powered)
        return 0;
	// 1. 准备电源
    if(cam->power_gpio)
		gpiod_set_value_cansleep(cam->power_gpio, 1);
    // 等待成功上电
	usleep_range(1000, 2000);
	// 2. 准备时钟
    // 设置时钟频率为24MHz
	ret = clk_set_rate(cam->xvclk, OV13850_XVCLK_FREQ);
	if (ret)
		dev_warn(dev, "failed to set xvclk to 24MHz: %d\n", ret);

	ret = clk_prepare_enable(cam->xvclk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable xvclk\n");  
	// 3. 复位摄像头模块
	if (cam->reset_gpio)
		gpiod_set_value_cansleep(cam->reset_gpio, 0);


	ret = regulator_bulk_enable(OV13850_NUM_SUPPLIES, cam->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators: %d\n", ret);
		goto disable_clk;
	}

	if (cam->reset_gpio)
		gpiod_set_value_cansleep(cam->reset_gpio, 1);
    // 等待复位完成
	usleep_range(500, 1000);
	// 4. 退出低功耗模式
    //  退出低功耗模式
	if (cam->pwdn_gpio)
		gpiod_set_value_cansleep(cam->pwdn_gpio, 1);
	// 5. 等待摄像头模块稳定
	/*
	 * 官方驱动注释里要求 first SCCB transaction 前等待 8192 cycles。
	 * 24MHz 下约 342us，这里保守等待 1~2ms。
	 */
	usleep_range(1000, 2000);    

	cam->powered = true;
	return 0;

disable_clk:
	clk_disable_unprepare(cam->xvclk);
	return ret;

}

static void ov13850_min_power_off(struct ov13850_min *cam)
{
	/*
	 * 下电按相反方向收回资源，并先让 sensor 进入 PWDN/reset，避免在时钟或
	 * 电源已经消失后继续向 MIPI 总线输出不完整数据。
	 */
	if (!cam->powered)
		return;
    // 开启低功耗
	if (cam->pwdn_gpio)
		gpiod_set_value_cansleep(cam->pwdn_gpio, 0);
    // 复位
	if (cam->reset_gpio)
		gpiod_set_value_cansleep(cam->reset_gpio, 0);
    // 关闭电源
	regulator_bulk_disable(OV13850_NUM_SUPPLIES, cam->supplies);
    // 关闭时钟
	clk_disable_unprepare(cam->xvclk);
    // 关闭电源GPIO
	if (cam->power_gpio)
		gpiod_set_value_cansleep(cam->power_gpio, 0);
    // 电源状态关闭
	cam->powered = false;
}

static int ov13850_min_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);

	return ov13850_min_power_on(cam);
}

static int ov13850_min_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);

	ov13850_min_power_off(cam);

	return 0;
}

static const struct dev_pm_ops ov13850_min_pm_ops = {
	SET_RUNTIME_PM_OPS(ov13850_min_runtime_suspend,
			   ov13850_min_runtime_resume, NULL)
};

/*
 * stream-on 的业务事务：
 *   global init -> mode registers -> replay controls -> 0x0100=1。
 *
 * 每次从 runtime suspend 回来 sensor 都可能丢失寄存器，因此不能假设 probe
 * 阶段写过一次就永久有效。controls 放在 mode 之后重放，保证用户设置的曝光、
 * 增益、VBLANK 和测试图覆盖模式表中的默认值。
 */
static int ov13850_min_start_streaming(struct ov13850_min *cam)
{
	int ret;

	ret = ov13850_min_apply_global_init(cam);
	if (ret)
		return ret;

	ret = ov13850_min_apply_mode(cam);
	if (ret)
		return ret;

	/*
	 * ctrl_handler 使用 cam->lock；s_stream() 已持有该锁。
	 * 必须先解锁，否则 handler_setup() 会再次申请同一把锁而死锁。
	 */
	mutex_unlock(&cam->lock);
	ret = v4l2_ctrl_handler_setup(&cam->ctrl_handler);
	mutex_lock(&cam->lock);
	if (ret)
		return ret;

	ret = ov13850_min_write_reg(cam->client, 0x0100, 0x01);
	if (ret) {
		dev_err(&cam->client->dev,
			"failed to start streaming: %d\n", ret);
		return ret;
	}

	dev_info(&cam->client->dev, "stream on\n");

	return 0;
}

static int ov13850_min_stop_streaming(struct ov13850_min *cam)
{
	int ret;

	// 0x0100 = 0x00：software standby，不输出图像
	ret = ov13850_min_write_reg(cam->client, 0x0100, 0x00);
	if (ret) {
		dev_err(&cam->client->dev,
			"failed to stop streaming: %d\n", ret);
		return ret;
	}

	dev_info(&cam->client->dev, "stream off\n");

	return 0;
}

static int ov13850_min_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov13850_min *cam = to_ov13850_min(sd);
	int ret = 0;

	mutex_lock(&cam->lock);
	// 规范为 0 或 1
	on = !!on;

	if (on == cam->streaming)
		goto unlock;

	/*
	 * runtime-PM 引用覆盖完整 streaming 生命周期：stream-on 获取引用并上电，
	 * stream-off 写入 standby 后释放引用。这样采集中 sensor 不会被自动下电，
	 * 空闲时又能回到 suspended/usage=0。
	 */
	if (on) {
		ret = pm_runtime_get_sync(&cam->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&cam->client->dev);
			goto unlock;
		}

		ret = ov13850_min_start_streaming(cam);
		if (ret) {
			pm_runtime_put(&cam->client->dev);
			goto unlock;
		}
	} else {
		ret = ov13850_min_stop_streaming(cam);
		if (ret)
			goto unlock;

		pm_runtime_put(&cam->client->dev);
	}

	cam->streaming = on;

unlock:
	mutex_unlock(&cam->lock);

	return ret;
}

static int ov13850_min_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13850_min *cam =
		container_of(ctrl->handler, struct ov13850_min,
			     ctrl_handler);
	struct i2c_client *client = cam->client;
	s64 exposure_max;
	u8 test_pattern;
	int ret = 0;

	/*
	 * VBLANK 会改变一帧总行数 VTS，因此曝光上限也必须同步改变，并保留
	 * 16 行安全余量。这个软件约束即使 sensor 当前断电也要立即更新。
	 */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		exposure_max = cam->cur_mode->height + ctrl->val - 16;
		__v4l2_ctrl_modify_range(cam->exposure,
					 cam->exposure->minimum,
					 exposure_max,
					 cam->exposure->step,
					 cam->exposure->default_value);
		break;
	}

	/*
	 * sensor 未上电时只保存 control 值，不为一次 control 写入单独唤醒硬件。
	 * 下次 stream-on 的 v4l2_ctrl_handler_setup() 会统一重放。若设备正在使用，
	 * 才把新值立即写到寄存器。
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ov13850_min_write_reg24(client,
					       OV13850_REG_EXPOSURE,
					       ctrl->val << 4);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13850_min_write_reg(client, OV13850_REG_GAIN_H,
					     (ctrl->val >> OV13850_GAIN_H_SHIFT) &
					     OV13850_GAIN_H_MASK);
		if (ret)
			break;

		ret = ov13850_min_write_reg(client, OV13850_REG_GAIN_L,
					     ctrl->val & OV13850_GAIN_L_MASK);
		break;

	case V4L2_CID_VBLANK:
		ret = ov13850_min_write_reg16(client, OV13850_REG_VTS,
					       cam->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val)
			test_pattern = OV13850_TEST_PATTERN_ENABLE |
				       (ctrl->val - 1);
		else
			test_pattern = OV13850_TEST_PATTERN_DISABLE;

		ret = ov13850_min_write_reg(client,
					     OV13850_REG_TEST_PATTERN,
					     test_pattern);
		break;

	default:
		dev_warn(&client->dev, "unhandled control 0x%x\n", ctrl->id);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13850_min_ctrl_ops = {
	.s_ctrl = ov13850_min_set_ctrl,
};

static int ov13850_min_init_controls(struct ov13850_min *cam)
{
	/*
	 * controls 把芯片时序能力转换成标准 V4L2 用户接口。LINK_FREQ/PIXEL_RATE/
	 * HBLANK 描述固定链路，VBLANK/EXPOSURE/GAIN/TEST_PATTERN 可由用户修改。
	 * handler 与 cam->lock 共用同一把锁，使 control 与 set_fmt/s_stream 串行。
	 */
	struct v4l2_ctrl_handler *handler = &cam->ctrl_handler;
	const struct ov13850_mode *mode = cam->cur_mode;
	struct v4l2_ctrl *link_freq;
	s64 vblank_def;
	s64 exposure_max;
	u32 hblank;
	int ret;

	ret = v4l2_ctrl_handler_init(handler, 7);
	if (ret)
		return ret;

	handler->lock = &cam->lock;

	link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE(ov13850_link_freq_menu_items) - 1,
					   mode->link_freq_idx,
					   ov13850_link_freq_menu_items);
	if (link_freq)
		link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  OV13850_PIXEL_RATE, OV13850_PIXEL_RATE,
			  1, OV13850_PIXEL_RATE);

	hblank = mode->hts_def - mode->width;
	cam->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					hblank, hblank, 1, hblank);
	if (cam->hblank)
		cam->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	cam->vblank = v4l2_ctrl_new_std(handler, &ov13850_min_ctrl_ops,
					V4L2_CID_VBLANK,
					vblank_def,
					OV13850_VTS_MAX - mode->height,
					1, vblank_def);

	exposure_max = mode->vts_def - 16;
	cam->exposure = v4l2_ctrl_new_std(handler, &ov13850_min_ctrl_ops,
					  V4L2_CID_EXPOSURE,
					  OV13850_EXPOSURE_MIN,
					  exposure_max,
					  OV13850_EXPOSURE_STEP,
					  mode->exp_def);

	cam->anal_gain = v4l2_ctrl_new_std(handler, &ov13850_min_ctrl_ops,
					   V4L2_CID_ANALOGUE_GAIN,
					   OV13850_GAIN_MIN,
					   OV13850_GAIN_MAX,
					   OV13850_GAIN_STEP,
					   OV13850_GAIN_DEFAULT);

	cam->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler, &ov13850_min_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(ov13850_test_pattern_menu) - 1,
					     0, 0,
					     ov13850_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
		return ret;
	}

	cam->sd.ctrl_handler = handler;

	return 0;
}


/// SYSFS PART START  ///

static int ov13850_min_debug_pm_get(struct ov13850_min *cam)
{
	return pm_runtime_resume_and_get(&cam->client->dev);
}

static void ov13850_min_debug_pm_put(struct ov13850_min *cam)
{
	pm_runtime_put(&cam->client->dev);
}
static ssize_t chip_id_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	u32 val = 0;
	int ret;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);
	ret = ov13850_min_read_reg(client, OV13850_CHIP_ID_REG, 2, &val);
	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%04x\n", val);
}
// 设置一个只读文件，用于存储摄像头芯片的ID号
static DEVICE_ATTR_RO(chip_id);

static ssize_t revision_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	u32 val = 0;
	int ret;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);
	ret = ov13850_min_read_reg(client, OV13850_REVISION_REG, 1, &val);
	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%02x\n", val);
}
// 设置一个只读文件，用于存储摄像头芯片的版本号
static DEVICE_ATTR_RO(revision);

static ssize_t reg_addr_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	u16 reg;

	mutex_lock(&cam->lock);
	reg = cam->current_reg;
	mutex_unlock(&cam->lock);

	return sysfs_emit(buf, "0x%04x\n", reg);
}

static ssize_t reg_addr_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	unsigned int reg;
	int ret;
	// 将输入的字符串转换为无符号整数
	ret = kstrtouint(buf, 0, &reg);
	if (ret)
		return ret;

	if (reg > 0xffff)
		return -EINVAL;

	mutex_lock(&cam->lock);
	cam->current_reg = reg;
	mutex_unlock(&cam->lock);

	return count;
}
// 设置一个可读写文件，用于存储当前准备读写的寄存器地址
static DEVICE_ATTR_RW(reg_addr);


static ssize_t reg_value_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	u16 reg;
	u32 val = 0;
	int ret;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);
	reg = cam->current_reg;
	ret = ov13850_min_read_reg(client, reg, 1, &val);
	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	return sysfs_emit(buf, "reg[0x%04x] = 0x%02x\n", reg, val);
}

static ssize_t reg_value_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	unsigned int val;
	u16 reg;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (val > 0xff)
		return -EINVAL;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);

	if (cam->streaming) {
		ret = -EBUSY;
	} else {
		reg = cam->current_reg;
		ret = ov13850_min_write_reg(client, reg, val);
	}

	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	dev_info(dev, "write reg[0x%04x] = 0x%02x\n", reg, val);

	return count;
}
// 设置一个可读写文件，用于存储当前准备读写的寄存器的值
static DEVICE_ATTR_RW(reg_value);

static ssize_t array_test_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	unsigned int trigger;
	int ret;

	ret = kstrtouint(buf, 0, &trigger);
	if (ret)
		return ret;

	if (trigger != 1)
		return -EINVAL;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);

	if (cam->streaming)
		ret = -EBUSY;
	else
		ret =  ov13850_min_write_array(client, ov13850_safe_stop_regs);

	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	dev_info(dev, "safe array test applied\n");

	return count;
}
static DEVICE_ATTR_WO(array_test);

static ssize_t mode_info_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	const struct ov13850_mode *mode;

	mutex_lock(&cam->lock);
	mode = cam->cur_mode;
	mutex_unlock(&cam->lock);

	if (!mode)
		return sysfs_emit(buf, "no mode selected\n");

	return sysfs_emit(buf,
			  "name=%s\nwidth=%u\nheight=%u\nhts_def=%u\nvts_def=%u\nexp_def=%u\nlink_freq_idx=%u\npixel_rate=%llu\n",
			  mode->name,
			  mode->width,
			  mode->height,
			  mode->hts_def,
			  mode->vts_def,
			  mode->exp_def,
			  mode->link_freq_idx,
			  mode->pixel_rate);
}
static DEVICE_ATTR_RO(mode_info);

static ssize_t mode_apply_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	unsigned int trigger;
	int ret;

	ret = kstrtouint(buf, 0, &trigger);
	if (ret)
		return ret;

	if (trigger != 1)
		return -EINVAL;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);

	if (cam->streaming)
		ret = -EBUSY;
	else
		ret = ov13850_min_apply_mode(cam);

	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(mode_apply);

// SYSFS方法调用全局寄存器配置写入函数
static ssize_t full_init_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov13850_min *cam = ov13850_min_from_client(client);
	unsigned int trigger;
	int ret;

	ret = kstrtouint(buf, 0, &trigger);
	if (ret)
		return ret;

	if (trigger != 1)
		return -EINVAL;

	ret = ov13850_min_debug_pm_get(cam);
	if (ret < 0)
		return ret;

	mutex_lock(&cam->lock);

	if (cam->streaming)
		ret = -EBUSY;
	else
		ret = ov13850_min_apply_full_init(cam);

	mutex_unlock(&cam->lock);

	ov13850_min_debug_pm_put(cam);

	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(full_init);

// 设置一个属性组，将上面定义的属性文件添加到该组中， 这里面的变量通过DEVICE_ATTR_RW 这里创建
static struct attribute *ov13850_min_attrs[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_revision.attr,
	&dev_attr_reg_addr.attr,
	&dev_attr_reg_value.attr,
	&dev_attr_array_test.attr,
	&dev_attr_mode_info.attr,
	&dev_attr_mode_apply.attr,
	&dev_attr_full_init.attr,
	NULL,
};

static const struct attribute_group ov13850_min_attr_group = {
	.attrs = ov13850_min_attrs,
};



/// SYSFS PART END  ///

/*
 * Pad operations 是 sensor 与 CIF/ISP 协商能力的接口。这里只暴露一个 source
 * pad、RAW10 BGGR media-bus code 和两种离散模式；它描述的是 MIPI 总线上传输
 * 的 RAW 数据，不是 ISP 处理后的 NV12。
 */
static int ov13850_enum_mbus_code(struct v4l2_subdev *sd, 
					struct v4l2_subdev_state *state,
			    struct v4l2_subdev_mbus_code_enum *code)
{
	if(code->pad != OV13850_PAD_SOURCE)
		return -EINVAL;
	if(code->index > 0)
		return -EINVAL;

	code->code = OV13850_MBUS_CODE;
	return 0;
}

static int ov13850_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	const struct ov13850_mode *mode;

	if (fse->pad != OV13850_PAD_SOURCE)
		return -EINVAL;

	if (fse->index >= ARRAY_SIZE(ov13850_min_supported_modes))
		return -EINVAL;

	if (fse->code != OV13850_MBUS_CODE)
		return -EINVAL;

	mode = ov13850_min_supported_modes[fse->index];

	fse->min_width = mode->width;
	fse->max_width = mode->width;
	fse->min_height = mode->height;
	fse->max_height = mode->height;

	return 0;
}

static int ov13850_enum_frame_interval(
                struct v4l2_subdev *sd,
                struct v4l2_subdev_state *state,
                struct v4l2_subdev_frame_interval_enum *fie)
{
    const struct ov13850_mode *mode;

    if (fie->pad != OV13850_PAD_SOURCE)
        return -EINVAL;

    if (fie->index >= ARRAY_SIZE(ov13850_min_supported_modes))
        return -EINVAL;

    /*
     * Rockchip CIF notifier 会把 fie 清零后只设置 index/pad，再期待 sensor
     * 回填 code、尺寸和 interval，因此 code=0 表示“请枚举”，不能拒绝。
     */
    if (fie->code && fie->code != OV13850_MBUS_CODE)
        return -EINVAL;

    mode = ov13850_min_supported_modes[fie->index];

    fie->code = OV13850_MBUS_CODE;
    fie->width = mode->width;
    fie->height = mode->height;
    fie->interval = mode->max_fps;

    return 0;
}


static int ov13850_min_get_reso_dist(const struct ov13850_mode *mode,
                                     struct v4l2_mbus_framefmt *framefmt)
{
    return abs(mode->width - framefmt->width) +
           abs(mode->height - framefmt->height);
}

static const struct ov13850_mode *
ov13850_min_find_best_fit(struct v4l2_subdev_format *fmt)
{
	/*
	 * sensor 只支持离散模式。用户请求任意尺寸时，以宽高差之和选择最近模式，
	 * 并把实际可用尺寸回填给调用者；这里不会让 sensor 任意缩放。
	 */
    struct v4l2_mbus_framefmt *framefmt = &fmt->format;
    const struct ov13850_mode *mode;
    const struct ov13850_mode *best_mode;
    int best_dist = -1;
    int dist;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(ov13850_min_supported_modes); i++) {
        mode = ov13850_min_supported_modes[i];
        dist = ov13850_min_get_reso_dist(mode, framefmt);

        if (best_dist == -1 || dist < best_dist) {
            best_dist = dist;
            best_mode = ov13850_min_supported_modes[i];
        }
    }

    return best_mode;
}


static int ov13850_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	struct ov13850_min *cam = to_ov13850_min(sd);
	int ret = 0;
	if (fmt->pad != OV13850_PAD_SOURCE)
		return -EINVAL;

	mutex_lock(&cam->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
	#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format =
			*v4l2_subdev_get_try_format(sd, state, fmt->pad);
	#else
		ret = -ENOTTY;
	#endif
	} else {
		fmt->format = cam->fmt;
	}
	mutex_unlock(&cam->lock);

	return ret;
}

static int ov13850_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	struct ov13850_min *cam = to_ov13850_min(sd);
	const struct ov13850_mode *mode;
	s64 exposure_max;
	s64 vblank_def;
	u32 hblank;
	int ret = 0;

	if (fmt->pad != OV13850_PAD_SOURCE)
		return -EINVAL;

	mutex_lock(&cam->lock);

	/*
	 * TRY format 只属于当前打开句柄的协商草稿，不改变 cam->cur_mode、controls
	 * 或硬件。ACTIVE format 才更新设备全局状态，并在 streaming 时返回 EBUSY，
	 * 防止一边传输帧一边更换 sensor 时序。
	 */
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mode = ov13850_min_find_best_fit(fmt);

		fmt->format.code = OV13850_MBUS_CODE;
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->format.colorspace = V4L2_COLORSPACE_RAW;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, state, fmt->pad) = fmt->format;
#else
		ret = -ENOTTY;
#endif

		mutex_unlock(&cam->lock);
		return ret;
	}

	if (cam->streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	mode = ov13850_min_find_best_fit(fmt);

	fmt->format.code = OV13850_MBUS_CODE;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;

	/*
	 * 选择新 ACTIVE mode 后，HBLANK、VBLANK 和曝光范围都依赖新的 HTS/VTS，
	 * 必须作为同一状态更新一起调整。寄存器仍留到下次 stream-on 再写。
	 */
	cam->cur_mode = mode;
	cam->fmt = fmt->format;

	hblank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(cam->hblank,
				hblank, hblank, 1, hblank);

	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(cam->vblank,
				vblank_def,
				OV13850_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 16;
	__v4l2_ctrl_modify_range(cam->exposure,
				OV13850_EXPOSURE_MIN,
				exposure_max,
				OV13850_EXPOSURE_STEP,
				mode->exp_def);

unlock:
	mutex_unlock(&cam->lock);

	return ret;
}

static int ov13850_min_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	if (pad_id != OV13850_PAD_SOURCE)
		return -EINVAL;

	/* 向接收端声明物理连接是 2-lane CSI-2 D-PHY。 */
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->bus.mipi_csi2.flags = 0;
	config->bus.mipi_csi2.num_data_lanes = OV13850_LANES;

	return 0;
}


static int ov13850_min_g_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *fi)
{
	struct ov13850_min *cam = to_ov13850_min(sd);

	mutex_lock(&cam->lock);
	fi->interval = cam->cur_mode->max_fps;
	mutex_unlock(&cam->lock);

	return 0;
}

static const struct v4l2_subdev_video_ops ov13850_video_ops = {
	.g_frame_interval = ov13850_min_g_frame_interval,
	.s_stream = ov13850_min_s_stream,
};


static const struct v4l2_subdev_pad_ops ov13850_pad_ops = {
	.enum_mbus_code = ov13850_enum_mbus_code,
	.enum_frame_size = ov13850_enum_frame_size,
	.enum_frame_interval = ov13850_enum_frame_interval,
	.get_fmt = ov13850_get_fmt,
	.set_fmt = ov13850_set_fmt,
	.get_mbus_config = ov13850_min_get_mbus_config,
};

static const struct v4l2_subdev_ops ov13850_subdev_ops = {
	.video = &ov13850_video_ops,
	.pad = &ov13850_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov13850_min_open(struct v4l2_subdev *sd,
			    struct v4l2_subdev_fh *fh)
{
	struct ov13850_min *cam = to_ov13850_min(sd);
	const struct ov13850_mode *mode =
		ov13850_min_supported_modes[0];
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->state,
					   OV13850_PAD_SOURCE);

	mutex_lock(&cam->lock);

	/* 每个新 subdev 文件句柄从默认模式获得独立 TRY format。 */
	try_fmt->width = mode->width;
	try_fmt->height = mode->height;
	try_fmt->code = OV13850_MBUS_CODE;
	try_fmt->field = V4L2_FIELD_NONE;
	try_fmt->colorspace = V4L2_COLORSPACE_RAW;

	mutex_unlock(&cam->lock);

	return 0;
}

static const struct v4l2_subdev_internal_ops
ov13850_min_internal_ops = {
	.open = ov13850_min_open,
};
#endif

static int ov13850_min_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov13850_min *cam;
	u32 chip_id = 0;
	u32 revision = 0;
	int ret;
	int i;

    /*
     * probe 建立“硬件存在 -> V4L2 可发现”的完整关系：
     *   取得板级资源 -> 临时上电 -> 校验 ID/revision -> 初始化 controls/
     *   media entity -> 注册异步 sensor subdev -> 启用 runtime PM。
     * 任一步失败都按创建顺序的反方向回滚。
     */
    dev_info(dev, "probe start, i2c addr=0x%02x\n", client->addr);

    // check if the I2C adapter supports plain I2C functionality
    if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
		return dev_err_probe(dev, -EIO, "adapter does not support plain I2C\n");

    // 分配摄像头设备结构体内存， GFP_KERNEL申请内存的分配标志
	cam = devm_kzalloc(dev, sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;
    // 绑定I2C客户端和摄像头设备结构体
	cam->client = client;
	cam->current_reg = OV13850_CHIP_ID_REG;

	cam->cur_mode = ov13850_min_supported_modes[0];
	// 初始化目前FMT
	cam->fmt.code = OV13850_MBUS_CODE;
	cam->fmt.width = cam->cur_mode->width;
	cam->fmt.height = cam->cur_mode->height;
	cam->fmt.field = V4L2_FIELD_NONE;
	cam->fmt.colorspace = V4L2_COLORSPACE_RAW;

    // 获取外部时钟资源
	cam->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(cam->xvclk))
		return dev_err_probe(dev, PTR_ERR(cam->xvclk), "failed to get xvclk\n");
    // 获取电源引脚资源， 并默认设置为低电平
	cam->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(cam->power_gpio))
		return dev_err_probe(dev, PTR_ERR(cam->power_gpio), "failed to get power-gpios\n");
    // 获取复位引脚资源，并默认设置为低电平
	cam->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(cam->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(cam->reset_gpio), "failed to get reset-gpios\n");
    // 获取低功耗引脚资源，并默认设置为低电平
	cam->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(cam->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(cam->pwdn_gpio), "failed to get pwdn-gpios\n");
    // 绑定电源名称和电源结构体
	for (i = 0; i < OV13850_NUM_SUPPLIES; i++)
		cam->supplies[i].supply = ov13850_supply_names[i];

	ret = devm_regulator_bulk_get(dev, OV13850_NUM_SUPPLIES, cam->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	mutex_init(&cam->lock);

    // 上电摄像头模块
	ret = ov13850_min_power_on(cam);
	if (ret)
		goto err_destroy_mutex;

    // 读取芯片ID寄存器的值，并与预期的芯片ID进行比较
	ret = ov13850_min_read_reg(client, OV13850_CHIP_ID_REG, 2, &chip_id);
	if (ret) {
		dev_err(dev, "failed to read chip id: %d\n", ret);
		goto err_power_off;
	}
	dev_info(dev, "chip id read from 0x300a = 0x%04x\n", chip_id);
	if (chip_id != OV13850_CHIP_ID) {
		dev_err(dev, "unexpected chip id 0x%04x, expected 0x%04x\n",
			chip_id, OV13850_CHIP_ID);
		ret = -ENODEV;
		goto err_power_off;
	}
    // 读取芯片版本寄存器的值
	ret = ov13850_min_read_reg(client, OV13850_REVISION_REG, 1, &revision);
	if (ret) {
		dev_err(dev, "failed to read revision: %d\n", ret);
		goto err_power_off;
	}

	dev_info(dev, "OV13850 detected successfully, revision=0x%02x\n", revision);
	ret = ov13850_min_select_global_regs(cam);
	if (ret)
		goto err_power_off;

	// 初始化 V4L2 sub_dev，绑定 子设备操作函数
	// 由于这个函数会在内部调用i2c_set_clientdata，所以上面不需要调用一次
	// 切这个函数后面保存的是一个void指针, 因此如果前面被调用过一次i2c_set_clientdata，
	// 这里就会覆盖掉前面的指针, 因此后面get_clientdata只会是返回 sd 这个结构体变量的指针
	v4l2_i2c_subdev_init(&cam->sd, client, &ov13850_subdev_ops);

	ret = ov13850_min_init_controls(cam);
	if (ret) {
		dev_err(dev, "failed to initialize controls: %d\n", ret);
		goto err_power_off;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	cam->sd.internal_ops = &ov13850_min_internal_ops;
#endif

	cam->sd.owner = THIS_MODULE;
	cam->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	cam->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	cam->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&cam->sd.entity, OV13850_NUM_PADS, &cam->pad);
	if (ret) {
		dev_err(dev, "failed to init media entity pads: %d\n", ret);
		goto err_free_handler;
	}
	
	ret = v4l2_async_register_subdev_sensor(&cam->sd);
	if (ret) {
		dev_err(dev, "failed to register v4l2 sensor subdev: %d\n", ret);
		goto err_cleanup_entity;
	}

	dev_info(dev, "v4l2 sensor subdev registered\n");

	// 创建 sysfs 属性组，包含芯片ID、版本号、寄存器地址和寄存器值的属性文件
	ret = sysfs_create_group(&dev->kobj, &ov13850_min_attr_group);
	if (ret) {
		dev_err(dev, "failed to create sysfs group: %d\n", ret);
		goto err_unregister_subdev;
	}
	dev_info(dev, "sysfs ready: chip_id revision reg_addr reg_value array_test mode_info mode_apply full_init\n");

	/*
	 * probe 此刻硬件仍处于上电状态，所以先标记 active，再启用 runtime PM。
	 * pm_runtime_idle() 随后允许框架在没有使用者时调用 runtime_suspend 下电。
	 */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

// 错误处理，清理资源的goto需要跟上面函数的调用顺序相反， 保证所有资源都能被正确释放

err_unregister_subdev:
	v4l2_async_unregister_subdev(&cam->sd);

err_cleanup_entity:
	media_entity_cleanup(&cam->sd.entity);

err_free_handler:
	v4l2_ctrl_handler_free(&cam->ctrl_handler);

err_power_off:
	ov13850_min_power_off(cam);

err_destroy_mutex:
	mutex_destroy(&cam->lock);

	return ret;
}

static void ov13850_min_remove(struct i2c_client *client)
{
	/*
	 * remove 先从用户可见的 sysfs/media graph 注销，再关闭 runtime PM 和硬件。
	 * 如果设备已经 suspended，就不能重复执行电源关闭序列。
	 */
    // 获取之前保存进去的驱动私有数据 cam
    struct ov13850_min *cam = ov13850_min_from_client(client);
	// 注销 V4L2 子设备
	sysfs_remove_group(&client->dev.kobj, &ov13850_min_attr_group);
	v4l2_async_unregister_subdev(&cam->sd);
	media_entity_cleanup(&cam->sd.entity);
	v4l2_ctrl_handler_free(&cam->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov13850_min_power_off(cam);

	pm_runtime_set_suspended(&client->dev);
	mutex_destroy(&cam->lock);

	dev_info(&client->dev, "removed\n");
}

// 绑定设备树
static const struct of_device_id ov13850_min_of_match[] = {
	{ .compatible = "learning,ov13850-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov13850_min_of_match);
// 绑定I2C设备ID，这种方法没有使用设备树的情况下可以使用
static const struct i2c_device_id ov13850_min_id[] = {
	{ "ov13850_i2c_min", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov13850_min_id);

// 前面写的 probe()、remove()、of_match_table、id_table，最后都要通过这里交给 Linux I2C core。
static struct i2c_driver ov13850_min_driver = {
	.driver = {
		.name = "ov13850_i2c_min",
		.of_match_table = ov13850_min_of_match,
		.pm = &ov13850_min_pm_ops,
	},
	.probe = ov13850_min_probe,
	.remove = ov13850_min_remove,
	.id_table = ov13850_min_id,
};

module_i2c_driver(ov13850_min_driver);

MODULE_DESCRIPTION("Minimal OV13850 I2C register read driver");
MODULE_LICENSE("GPL");
