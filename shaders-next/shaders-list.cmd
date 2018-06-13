:: It is helper for compilation shaders to SPIR-V

set VRTX=vertex\
set RNDR=rayTracing\
set HLBV=hlBVH2\
set RDXI=qRadix\
set OUTP=output\

set CMPPROF=-fshader-stage=compute
set FRGPROF=-fshader-stage=fragment
set VRTPROF=-fshader-stage=vertex
set GMTPROF=-fshader-stage=geometry

mkdir %OUTDIR%
mkdir %OUTDIR%%VRTX%
mkdir %OUTDIR%%RNDR%
mkdir %OUTDIR%%HLBV%
mkdir %OUTDIR%%RDXI%
mkdir %OUTDIR%%OUTP%
mkdir %OUTDIR%%GENG%
mkdir %OUTDIR%%HLBV%next-gen-sort


start /b /wait glslangValidator %CFLAGSV% %INDIR%%OUTP%render.frag        -o %OUTDIR%%OUTP%render.frag.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%OUTP%render.vert        -o %OUTDIR%%OUTP%render.vert.spv

start /b /wait glslangValidator %CFLAGSV% %INDIR%%RNDR%closest-hit-shader.comp -o %OUTDIR%%RNDR%closest-hit-shader.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%RNDR%generation-shader.comp  -o %OUTDIR%%RNDR%generation-shader.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%RNDR%miss-hit-shader.comp    -o %OUTDIR%%RNDR%miss-hit-shader.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%RNDR%resolve-shader.comp     -o %OUTDIR%%RNDR%resolve-shader.comp.spv

start /b /wait glslangValidator %CFLAGSV% %INDIR%%VRTX%vinput.comp        -o %OUTDIR%%VRTX%vinput.comp.spv

start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%bound-calc.comp    -o %OUTDIR%%HLBV%bound-calc.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%bvh-build.comp     -o %OUTDIR%%HLBV%bvh-build.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%bvh-fit.comp       -o %OUTDIR%%HLBV%bvh-fit.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%leaf-gen.comp      -o %OUTDIR%%HLBV%leaf-gen.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%leaf-link.comp     -o %OUTDIR%%HLBV%leaf-link.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%shorthand.comp     -o %OUTDIR%%HLBV%shorthand.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%HLBV%traverse-bvh.comp  -o %OUTDIR%%HLBV%traverse-bvh.comp.spv

start /b /wait glslangValidator %CFLAGSV% %INDIR%%RDXI%permute.comp       -o %OUTDIR%%RDXI%permute.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%RDXI%histogram.comp     -o %OUTDIR%%RDXI%histogram.comp.spv
start /b /wait glslangValidator %CFLAGSV% %INDIR%%RDXI%pfx-work.comp      -o %OUTDIR%%RDXI%pfx-work.comp.spv

:: --ccp not supported by that renderer 

set FIXFLAGS = ^
--skip-validation ^
--strip-debug ^
--workaround-1209 ^
--replace-invalid-opcode ^
--simplify-instructions ^
--cfg-cleanup ^
-Os

set OPTFLAGS= ^
--skip-validation ^
--private-to-local ^
--ccp ^
--unify-const ^
--flatten-decorations ^
--fold-spec-const-op-composite ^
--strip-debug ^
--freeze-spec-const ^
--cfg-cleanup ^
--merge-blocks ^
--merge-return ^
--strength-reduction ^
--inline-entry-points-exhaustive ^
--convert-local-access-chains ^
--eliminate-dead-code-aggressive ^
--eliminate-dead-branches ^
--eliminate-dead-const ^
--eliminate-dead-variables ^
--eliminate-dead-functions ^
--eliminate-local-single-block ^
--eliminate-local-single-store ^
--eliminate-local-multi-store ^
--eliminate-common-uniform ^
--eliminate-insert-extract ^
--scalar-replacement ^
--relax-struct-store ^
--redundancy-elimination ^
--remove-duplicates ^
--private-to-local ^
--local-redundancy-elimination ^
--cfg-cleanup ^
--workaround-1209 ^
--replace-invalid-opcode ^
--if-conversion ^
--scalar-replacement ^
--simplify-instructions
