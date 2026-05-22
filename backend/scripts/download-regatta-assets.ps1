# Download Virtual Eye textures, skybox, terrain, and race data.
# Usage: .\download-regatta-assets.ps1 [viewer-folder]
param([string]$ViewerDir = "")
$ErrorActionPreference = "Stop"
$BaseUrl = "https://dx6j99ytnx80e.cloudfront.net"
if ($ViewerDir) {
    $Root = (Resolve-Path $ViewerDir).Path
} else {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "..\public\regatta-viewer")).Path
}
Write-Host "Downloading into $Root"

function Ensure-Dir($path) {
    if (-not (Test-Path $path)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
}

function Download-File($relativePath) {
    $dest = Join-Path $Root $relativePath
    Ensure-Dir (Split-Path $dest -Parent)
    $url = "$BaseUrl/$($relativePath -replace '\\','/')"
    if (Test-Path $dest) {
        Write-Host "skip $relativePath"
        return
    }
    Write-Host "get  $relativePath"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
}

$textures = @(
    "textures/TP52WakeFoam.png",
    "textures/RippledWater_Greyscale_NRM_F_DISP.png",
    "textures/RippledWater_Greyscale_NRM_F_NRM.png",
    "textures/WaterBaseLayer.png",
    "textures/communityWaterFoam.png",
    "textures/waterFresnel.png",
    "textures/waterGrid.png",
    "textures/BoatBlobShadow.png",
    "textures/traildot_transparent_solid.png",
    "textures/trajLineTex.png",
    "textures/windarrow-02.png",
    "textures/AdantageStretchArrow.png",
    "textures/checkeredPattern.png",
    "textures/startLineTexture.png"
)

foreach ($t in $textures) { Download-File $t }

foreach ($face in @("px", "nx", "py", "ny", "pz", "nz")) {
    Download-File "textures/ClearBlue_SunLess/ClearBlue_$face.jpg"
}

Download-File "models/AucklandTerrain/AucklandOptimised.glb"

$terrainJpg = @(
    "CBD_front_new_pier.jpg", "CBD_wharf.jpg", "Combined_C.jpg", "Combined_A.jpg",
    "Outter_left.jpg", "Mid_right_pieces.jpg", "Bridge_end.jpg", "Rangatoto.jpg",
    "NewBit_2K.jpg", "extraTile_A.jpg", "Golf_harbor_marina.jpg", "HeadLandLeft.jpg",
    "North_Shore_beaches03.jpg", "Combined_B.jpg", "bridgetop.jpg", "Walls.jpg"
)
foreach ($jpg in $terrainJpg) {
    Download-File "models/AucklandTerrain/$jpg"
}

foreach ($font in @("e2a5e608ba42366402cbf775c06f3f15.OTF", "368d8d0ec421694172277d72614086ba.OTF")) {
    Download-File $font
}

$raceList = Join-Path $Root "racedata\acws2020\RacesList.dat"
if (Test-Path $raceList) {
    $bins = Select-String -Path $raceList -Pattern "^DataFile\s*=\s*(\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value }
    foreach ($bin in $bins) {
        Download-File "racedata/acws2020/$bin"
    }
}

Write-Host "Done. Assets under $Root"
