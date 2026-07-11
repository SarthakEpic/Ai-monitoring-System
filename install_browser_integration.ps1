param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-p]{32}$')]
    [string]$ExtensionId,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostPath = Join-Path $root 'PredictiveAutoHealBrowserHost.exe'
$manifestTemplate = Join-Path $root 'integrations\browser\native-host-manifest.json'
$installRoot = Join-Path $env:LOCALAPPDATA 'PredictiveAutoHeal\BrowserIntegration'
$manifestPath = Join-Path $installRoot 'com.predictive_autoheal.browser.json'
$registryPaths = @(
    'HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.predictive_autoheal.browser',
    'HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.predictive_autoheal.browser'
)

if ($Remove) {
    foreach ($registryPath in $registryPaths) {
        if (Test-Path -LiteralPath $registryPath) { Remove-Item -LiteralPath $registryPath -Force }
    }
    if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath -Force }
    Write-Host 'PredictiveAutoHeal browser native host registration removed.'
    exit 0
}

if (-not (Test-Path -LiteralPath $hostPath)) { throw "Native host not found: $hostPath" }
if (-not (Test-Path -LiteralPath $manifestTemplate)) { throw "Manifest template not found: $manifestTemplate" }
New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
$escapedHostPath = $hostPath.Replace('\', '\\')
$manifest = [IO.File]::ReadAllText($manifestTemplate)
$manifest = $manifest.Replace('REPLACE_WITH_ABSOLUTE_NATIVE_HOST_PATH', $escapedHostPath)
$manifest = $manifest.Replace('REPLACE_WITH_EXTENSION_ID', $ExtensionId)
[IO.File]::WriteAllText($manifestPath, $manifest, [Text.UTF8Encoding]::new($false))
foreach ($registryPath in $registryPaths) {
    New-Item -Path $registryPath -Force | Out-Null
    Set-Item -Path $registryPath -Value $manifestPath
}
Write-Host "Registered cooperative browser host for extension $ExtensionId."
