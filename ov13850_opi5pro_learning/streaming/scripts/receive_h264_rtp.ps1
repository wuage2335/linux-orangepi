param(
    [ValidateRange(1, 65535)]
    [int]$Port = 5004,

    [ValidateRange(0, 2000)]
    [int]$LatencyMs = 30,

    [ValidateSet('auto', 'hardware', 'software')]
    [string]$Decoder = 'auto'
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command 'gst-launch-1.0' -ErrorAction SilentlyContinue)) {
    $userBin = Join-Path $env:LOCALAPPDATA 'Programs\gstreamer\1.0\msvc_x86_64\bin'
    if (Test-Path -LiteralPath (Join-Path $userBin 'gst-launch-1.0.exe')) {
        $env:Path = "$userBin;$env:Path"
    }
}

if (-not (Get-Command 'gst-launch-1.0' -ErrorAction SilentlyContinue)) {
    throw 'gst-launch-1.0 was not found in PATH or the per-user install directory'
}

function Test-GstPlugin {
    param([Parameter(Mandatory = $true)][string]$Name)

    & gst-inspect-1.0 $Name *> $null
    return $LASTEXITCODE -eq 0
}

$hasHardware = (Test-GstPlugin 'd3d11h264dec') -and (Test-GstPlugin 'd3d11videosink')
$hasSoftware = Test-GstPlugin 'avdec_h264'

if ($Decoder -eq 'hardware' -and -not $hasHardware) {
    throw 'requested D3D11 decoder/sink is unavailable'
}
if ($Decoder -eq 'software' -and -not $hasSoftware) {
    throw 'requested avdec_h264 is unavailable'
}

$useHardware = $Decoder -eq 'hardware' -or ($Decoder -eq 'auto' -and $hasHardware)
if (-not $useHardware -and -not $hasSoftware) {
    throw 'neither D3D11 nor avdec_h264 is available'
}

if ($useHardware) {
    $decoderElement = 'd3d11h264dec'
    $sinkElement = 'd3d11videosink'
} else {
    $decoderElement = 'avdec_h264'
    $sinkElement = 'autovideosink'
}

$caps = 'application/x-rtp,media=video,encoding-name=H264,' +
    'payload=96,clock-rate=90000'

Write-Host "receiver_port=$Port latency_ms=$LatencyMs"
Write-Host "decoder=$decoderElement sink=$sinkElement"

$pipeline = @(
    '-v',
    'udpsrc', "port=$Port", "caps=$caps",
    '!', 'rtpjitterbuffer', "latency=$LatencyMs", 'drop-on-latency=true',
    '!', 'rtph264depay',
    '!', 'h264parse',
    '!', $decoderElement,
    '!', $sinkElement, 'sync=false'
)

& gst-launch-1.0 @pipeline
exit $LASTEXITCODE
