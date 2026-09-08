from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile
import fcntl
import re

parser=argparse.ArgumentParser()
parser.add_argument('name')
parser.add_argument('--mode',choices=('replay','coarse','native'),default='replay')
parser.add_argument('--output',type=Path,default=Path('build/vita-architecture'))
args=parser.parse_args()
root=Path.cwd()
if not re.fullmatch(r'[A-Za-z0-9_-]{1,48}',args.name):
    raise ValueError('Invalid artifact name')
out=(root/args.output).resolve()
if not out.is_relative_to((root/'build').resolve()):
    raise ValueError('Architecture outputs must be beneath build/')
source=Path(__file__).resolve().parent
if not source.is_relative_to(root):
    raise ValueError('Run the builder from the repository root')
out.mkdir(parents=True,exist_ok=True)
build_lock=(root/'build/vita-architecture.lock').open('a')
fcntl.flock(build_lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
args.replay=args.mode=='replay'
packages=out/'packages'
packages.mkdir(exist_ok=True)
for suffix in ('.vpk','.elf','.json'):
    if (packages/(args.name+suffix)).exists():
        raise FileExistsError(packages/(args.name+suffix))
image='dk64-vita-fps-final-noscratch'
image_id=subprocess.check_output(['docker','image','inspect',image,'--format','{{.Id}}'],text=True).strip()
assert image_id=='sha256:cac4379657612cad033a8a0b47026d394e4049a792a684995a45bab0f09719eb'
cmd=['docker','run','--rm','--platform','linux/amd64','-v',str(root)+':/project']
scoped={}
probe='/project/'+str(source.relative_to(root))
def replace_once(text,old,new):
    if text.count(old)!=1:raise ValueError('Overlay location is not unique: '+old)
    return text.replace(old,new,1)
for path in ('platform/vita/CMakeLists.txt','platform/vita/main.cpp','platform/vita/renderer_context.cpp','platform/host_probe/CMakeLists.txt'):
    data=subprocess.check_output(['git','show','HEAD:'+path])
    if args.replay:
        text=data.decode()
        if path=='platform/vita/CMakeLists.txt':
            text+='\nif(DK64_VITA_BENCHMARK)\n target_sources(DK64Recompiled PRIVATE '+probe+'/architecture_probe.cpp)\n target_include_directories(DK64Recompiled PRIVATE '+probe+')\nendif()\n'
        if path=='platform/vita/renderer_context.cpp':
            text='#include "architecture_probe.h"\n'+text
            text=replace_once(text,'sink = RT64::createFastVitaGLSink(false);','sink = ArchitectureProbe::createSink();')
            text=replace_once(text,'interpreter.loadUCodeGBI(task->t.ucode,task->t.ucode_data,true);','ArchitectureProbe::beginTask(state->RDRAM);\n            interpreter.loadUCodeGBI(task->t.ucode,task->t.ucode_data,true);')
            text=replace_once(text,'interpreter.processDisplayLists(address,reinterpret_cast<RT64::DisplayList *>(state->fromRDRAM(address)));','interpreter.processDisplayLists(address,reinterpret_cast<RT64::DisplayList *>(state->fromRDRAM(address)));\n            sink->flushDraws();\n            ArchitectureProbe::endTask();')
        data=text.encode()
    if args.mode=='native' and path=='platform/vita/CMakeLists.txt':
        wrappers=['sceGxmSetVertexProgram','sceGxmSetFragmentProgram','sceGxmReserveVertexDefaultUniformBuffer','sceGxmReserveFragmentDefaultUniformBuffer','sceGxmSetVertexDefaultUniformBuffer','sceGxmSetFragmentDefaultUniformBuffer']
        data+=('\nadd_executable(rt64_architecture_control '+probe+'/precompute_control.cpp)\n'
               'target_compile_features(rt64_architecture_control PRIVATE cxx_std_17)\n'
               'target_link_libraries(rt64_architecture_control PRIVATE rt64 "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive" ScePower_stub m)\n'
               'target_link_options(rt64_architecture_control PRIVATE -Wl,-z,max-page-size=0x10000 '+ ' '.join('-Wl,--wrap='+w for w in wrappers)+')\n'
               'vita_create_self(rt64_architecture_control.self rt64_architecture_control UNSAFE)\n'
               'vita_create_vpk(RT64Architecture.vpk RT64A0001 rt64_architecture_control.self VERSION 01.00 NAME "RT64 Architecture Control")\n').encode()
    target=out/'scoped-source'/args.name/path
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() and target.read_bytes()!=data:
        raise ValueError('Scoped source changed: '+path)
    target.write_bytes(data)
    scoped[path]=hashlib.sha256(data).hexdigest()
    cmd+=['-v',str(target)+':/project/'+path+':ro']

if args.replay:
    overlays={}
    for path in ('lib/rt64/src/fast/rt64_fast_rsp.cpp','lib/rt64/src/fast/rt64_fast_rdp.cpp','lib/rt64/src/fast/rt64_fast_vitagl.cpp','lib/rt64/src/fast/rt64_fast_interpreter.cpp','platform/vita/fps_benchmark.cpp'):
        text=Path(path).read_text()
        if path.endswith('rt64_fast_rsp.cpp'):
            text='#include "'+probe+'/architecture_probe.h"\n'+text
            text=replace_once(text,'const auto *input=state->fromRDRAM(address,count*16);','const auto *input=state->fromRDRAM(address,count*16);\n        ArchitectureProbe::observeVertex(address,input,count*16,geometryMode);')
            signatures=['void FastRSP::matrix(uint32_t address, uint8_t params) {','void FastRSP::popMatrix(uint32_t count) {','void FastRSP::forceMatrix(uint32_t address) {','void FastRSP::insertMatrix(uint32_t offset, uint32_t value) {','void FastRSP::modifyVertex(uint32_t index, uint32_t where, uint32_t value) {','void FastRSP::branchZ(uint32_t address, uint32_t index, uint32_t z, DisplayList **dl) {','void FastRSP::branchW(uint32_t address, uint32_t index, uint32_t w, DisplayList **dl) {','void FastRSP::setViewport(uint32_t address) {']
            for index,signature in enumerate(signatures):text=replace_once(text,signature,signature+'\n        ArchitectureProbe::observeOperation('+str(index)+');')
        elif path.endswith('rt64_fast_rdp.cpp'):
            text='#include "'+probe+'/architecture_probe.h"\n'+text
            text=replace_once(text,'state->fromRDRAM(start,span);','state->fromRDRAM(start,span);\n        ArchitectureProbe::observeTexture(start,state->RDRAM+start,uint32_t(span));')
        elif path.endswith('rt64_fast_interpreter.cpp'):
            text='#include "'+probe+'/architecture_probe.h"\n'+text
            text=replace_once(text,'state->fromRDRAM(offset, 8);','state->fromRDRAM(offset, 8);\n            ArchitectureProbe::observeCommand(offset,state->RDRAM,state->rdramSize);')
        elif path.endswith('rt64_fast_vitagl.cpp'):
            text='#include "'+probe+'/architecture_probe.h"\n'+text
            text=replace_once(text,'glBindBuffer(GL_ARRAY_BUFFER,vbo);\n            glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(FastVertex), v.data(), GL_STREAM_DRAW);','const GLuint saved=ArchitectureProbe::residentGeometry(v);\n            glBindBuffer(GL_ARRAY_BUFFER,saved?saved:vbo);\n            if(!saved)glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(FastVertex), v.data(), GL_STREAM_DRAW);')
            text=replace_once(text,'glDrawArrays(GL_TRIANGLES,0,v.size());','ArchitectureProbe::configureViewport();\n            glDrawArrays(GL_TRIANGLES,0,v.size());')
            text=replace_once(text,'const auto key=fastShaderKey(d);','auto key=fastShaderKey(d);\n            const bool simple=ArchitectureProbe::simpleFragment() && !d.fill;\n            const auto specialization=ArchitectureProbe::specializationId(d);\n            if(simple)key[3]|=0x40000000U;\n            if(specialization)key={specialization,0,0,0x20000000U};')
            text=replace_once(text,'makeProgram(vertexShader,fastFragmentShader(d))','makeProgram(vertexShader,specialization?ArchitectureProbe::specializedSource(d):simple?std::string("#version 100\\nprecision highp float; varying vec4 vColor; void main(){gl_FragColor=vColor;}\\n"):fastFragmentShader(d))')
        else:
            text=replace_once(text,'\\"compiled_stage_profiling\\":%s,','\\"architecture_probe\\":true,\\"compiled_stage_profiling\\":%s,')
        target=out/'scoped-source'/args.name/path
        target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
        scoped[path]=hashlib.sha256(target.read_bytes()).hexdigest()
        cmd+=['-v',str(target)+':/project/'+path+':ro']

def source_hashes():
    result={}
    paths=subprocess.check_output(['git','-C','lib/rt64','ls-files','-c','-o','--exclude-standard','-z']).split(b'\0')
    for item in paths:
        if not item:continue
        path=Path('lib/rt64')/item.decode()
        if path.is_file():result[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    for path in ('platform/vita/fps_benchmark.cpp','platform/vita/fps_benchmark.h','platform/vita/fps_benchmark_logic.h','platform/vita/cpu_smoke.cpp','platform/vita/depth_smoke.cpp'):
        result[path]=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    for path in source.glob('architecture_*.*'):
        if path.is_file():result[str(path.relative_to(root))]=hashlib.sha256(path.read_bytes()).hexdigest()
    native=source/'precompute_control.cpp'
    if native.exists():result[str(native.relative_to(root))]=hashlib.sha256(native.read_bytes()).hexdigest()
    return result
before=source_hashes()
benchmark=args.mode in ('replay','coarse')
profile=args.mode=='coarse'
build='build/vita-architecture-'+('benchmark' if benchmark else 'native')
flags={
    'DK64_VITA':'ON','DK64_VITA_RUNTIME':'ON','DK64_VITA_GAME':'ON',
    'DK64_VITA_DIAGNOSTICS':'OFF','DK64_VITA_BENCHMARK':'ON' if benchmark else 'OFF',
    'DK64_VITA_BENCHMARK_WATCHDOG':'OFF','DK64_VITA_PROFILE_FUNCTIONS':'OFF',
    'DK64_VITA_TRACE_RENDERER':'OFF','DK64_VITA_SCRIPTED_INPUT':'OFF',
    'DK64_VITA_SCRIPTED_PAUSE':'OFF','DK64_VITA_AUDIO_CAPTURE':'OFF','DK64_VITA_PROBE_MAP':'0',
    'DK64_VITA_PROBE_CAMERA':'OFF','RT64_FAST_VALIDATE_UPLOADS':'OFF',
    'RT64_FAST_PROFILE':'ON' if profile else 'OFF','RT64_FAST_PROFILE_COARSE':'ON' if profile else 'OFF',
    'RT64_FAST_REFERENCE_DRAW':'OFF','RT64_FAST_REFERENCE_TEXTURE_COMPARE':'OFF',
    'RT64_FAST_REFERENCE_TMEM_LOAD':'OFF',
    'RT64_FAST_REFERENCE_VERTEX_INPUT':'OFF',
    'RT64_FAST_READBACKS_SPEEDHACK':'ON','CMAKE_BUILD_TYPE':'Release'
}
config=' '.join('-D'+key+'='+value for key,value in flags.items())
package='DK64FPSBenchmark.vpk' if benchmark else 'DK64Recompiled.vpk'
executable='DK64Recompiled'
if args.mode=='cpu':package,executable='RT64CpuControl.vpk','rt64_cpu_smoke'
if args.mode=='depth':package,executable='RT64DepthControl.vpk','rt64_depth_smoke'
if args.mode=='native':package,executable='RT64Architecture.vpk','rt64_architecture_control'
for relative in ('rt64/src/fast/CMakeFiles/rt64.dir','platform/vita/CMakeFiles/'+executable+'.dir'):
    for path in (root/build/relative).rglob('*.obj'):
        path.unlink()
cmd+=[image,'sh','-c',f'cmake -S . -B {build} {config} && cmake --build {build} --target {package}-vpk -j6']
log=out/('build-'+args.name+'.log')
with log.open('w') as handle:
    result=subprocess.run(cmd,stdout=handle,stderr=subprocess.STDOUT)
assert before==source_hashes(),'Sources changed during build'
if result.returncode:
    raise SystemExit(result.returncode)
record={'image':image_id,'mode':args.mode,'architecture_probe':args.replay,'flags':flags,'scoped_source_sha256':scoped,'source_sha256':before}
for suffix,filename in (('vpk',package),('elf',executable)):
    target=packages/(args.name+'.'+suffix)
    shutil.copy2(root/build/'platform/vita'/filename,target)
    record[suffix+'_sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
with zipfile.ZipFile(packages/(args.name+'.vpk')) as archive:
    assert archive.testzip() is None
    record['eboot_sha256']=hashlib.sha256(archive.read('eboot.bin')).hexdigest()
for path,label in (('CMakeCache.txt','cache'),('platform/vita/CMakeFiles/DK64Recompiled.dir/flags.make','frontend'),('platform/vita/CMakeFiles/dk64_recompiled_vita.dir/flags.make','game'),('rt64/src/fast/CMakeFiles/rt64.dir/flags.make','renderer')):
    shutil.copy2(root/build/path,packages/(args.name+'-'+label+'.txt'))
(packages/(args.name+'.json')).write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({key:record[key] for key in ('image','mode','vpk_sha256','elf_sha256','eboot_sha256')},indent=2),flush=True)
