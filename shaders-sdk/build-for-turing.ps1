#!/snap/bin/pwsh-preview

$CFLAGSV="--target-env vulkan1.1 -V -d -t --aml --nsf -DUSE_MORTON_32 -DUSE_F32_BVH -DNVIDIA_PLATFORM -DENABLE_TURING_INSTRUCTION_SET"

$VNDR="turing"
. "./shaders-list.ps1"

BuildAllShaders ""
BuildRTXShaders ""

#pause for check compile errors
Pause