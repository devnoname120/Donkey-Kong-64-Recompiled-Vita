from pathlib import Path
import argparse
import fcntl
import json
import sys
from types import SimpleNamespace
sys.path.insert(0, 'tools')
import vita_fps_benchmark as benchmark
parser = argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--original',type=Path,required=True)
parser.add_argument('--host',default='192.168.1.146')
parser.add_argument('--scenario', choices=('attract', 'world'), default='attract')
parser.add_argument('--package',type=Path,required=True)
args = parser.parse_args()
run_id = 'a4-' + args.output.name
output = args.output
config = SimpleNamespace(host=args.host, package=args.package,
    original=args.original, rom=Path('donkeykong64.us.z64'),
    seed_directory=Path('build/fps-autobench/preflight/normal-saves'), output=output,
    run_id=run_id, scenario=args.scenario, seconds=120, profile_every=0)
with Path('build/fps-autobench/device.lock').open('a') as lock:
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    result = benchmark.run(config)
    with benchmark.Device(config.host) as device:
        data = device.get(benchmark.DATA + '/' + run_id + '-architecture.json', limit=4*1024*1024)
        (output / (run_id + '-architecture.json')).write_bytes(data)
        image_path=benchmark.DATA + '/' + run_id + '-architecture.ppm'
        if device.exists(image_path):
            (output / (run_id + '-architecture.ppm')).write_bytes(device.get(image_path,limit=4*1024*1024))
        assert device.get(benchmark.EBOOT) == benchmark.package_payload(config.original)
    evidence = json.loads(data)
    assert evidence['run'] == run_id and evidence['diagnostic_not_game_fps'] is True
    assert evidence['replay_exact_rgba'] and evidence['specialized_exact_rgba'] and not evidence['error']
    assert result['clean_shutdown_verified'] is True
    print(json.dumps(evidence, indent=2))
