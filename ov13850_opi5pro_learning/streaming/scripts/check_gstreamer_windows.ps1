$ErrorActionPreference = 'Stop'

function Test-NativeCommand {
    param([Parameter(Mandatory = $true)][string]$Name)

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Write-Host "MISSING: command $Name"
        return $false
    }

    Write-Host "COMMAND_OK=$Name"
    return $true
}

function Test-GstPlugin {
    param([Parameter(Mandatory = $true)][string]$Name)

    & gst-inspect-1.0 $Name *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "PLUGIN_OK=$Name"
        return $true
    }

    Write-Host "MISSING: GStreamer plugin $Name"
    return $false
}

$complete = $true
$hasLaunch = Test-NativeCommand 'gst-launch-1.0'
$hasInspect = Test-NativeCommand 'gst-inspect-1.0'
$complete = $complete -and $hasLaunch -and $hasInspect

if ($hasLaunch) {
    & gst-launch-1.0 --version
}

$requiredPlugins = @(
    'udpsrc',
    'rtpjitterbuffer',
    'rtph264depay',
    'h264parse',
    'autovideosink'
)

if ($hasInspect) {
    foreach ($plugin in $requiredPlugins) {
        $complete = (Test-GstPlugin $plugin) -and $complete
    }

    $hardwareDecoder = Test-GstPlugin 'd3d11h264dec'
    $hardwareSink = Test-GstPlugin 'd3d11videosink'
    $softwareDecoder = Test-GstPlugin 'avdec_h264'

    Write-Host "HARDWARE_DECODER_AVAILABLE=$hardwareDecoder"
    Write-Host "HARDWARE_SINK_AVAILABLE=$hardwareSink"
    Write-Host "SOFTWARE_DECODER_AVAILABLE=$softwareDecoder"

    if (-not $hardwareDecoder -and -not $softwareDecoder) {
        Write-Host 'MISSING: both hardware and software H.264 decoders'
        $complete = $false
    }
}

if (-not $complete) {
    Write-Host 'GSTREAMER_WINDOWS_ENV=INCOMPLETE'
    exit 1
}

Write-Host 'GSTREAMER_WINDOWS_ENV=OK'
