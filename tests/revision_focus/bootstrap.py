"""Use the existing private Audio/ASR/HTTP fixture with independent focus cases."""
import importlib.util
import runpy
import sys

assert len(sys.argv) == 3
spec = importlib.util.spec_from_file_location('revision_cases', '/opt/focus-tests/cases.py')
cases = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cases)
sys.modules['revision_cases'] = cases
runpy.run_path('/opt/hv/tests/retry_desktop_test.py', run_name='__main__')
