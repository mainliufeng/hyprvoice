import importlib.util
import runpy
import sys
spec = importlib.util.spec_from_file_location('revision_cases', '/opt/focus-tests/native_cases.py')
cases = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cases)
sys.modules['revision_cases'] = cases
runpy.run_path('/opt/hv/tests/retry_desktop_test.py', run_name='__main__')
