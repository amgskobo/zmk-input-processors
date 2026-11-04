# SPDX-License-Identifier: MIT
"""Run current native requeue assertions with unchanged prior compiler inputs."""
import argparse, io, json, os
from pathlib import Path
import subprocess, tarfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--workspace',type=Path,required=True)
p.add_argument('--baseline-revision',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=False)
source=a.output/'prior';source.mkdir()
archive=subprocess.check_output(['git','archive',a.baseline_revision,'CMakeLists.txt','Kconfig','drivers','include','dts','zephyr'],cwd=root)
with tarfile.open(fileobj=io.BytesIO(archive)) as bundle: bundle.extractall(source,filter='data')
result=subprocess.run(['bash',str(root/'tests/requeue/run.sh'),str(a.workspace)],env=dict(os.environ,ZMK_TEST_OUTPUT=str(a.output/'native'),ZMK_TEST_MODULE=str(source)),capture_output=True,text=True)
(a.output/'console.log').write_text(result.stdout+result.stderr)
assert result.returncode!=0, 'prior asynchronous cancellation unexpectedly passed'
for i in range(1,4):
 log=(a.output/f'native/run-{i}.log').read_text()
 assert 'activation requeue result=-16' in log and 'running cancellation rejected new activation' in log, log
(a.output/'result.json').write_text(json.dumps(dict(baseline=a.baseline_revision,exit_code=result.returncode,qualified_failures=3))+'\n')
print('PASS: unchanged prior driver rejected all three isolated activation requeues')
