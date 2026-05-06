import subprocess
import pytest
import os

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def run_script(script_path):
    full_path = os.path.join(ROOT_DIR, script_path)
    result = subprocess.run(["bash", full_path], cwd=ROOT_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"STDOUT:\n{result.stdout}")
        print(f"STDERR:\n{result.stderr}")
    assert result.returncode == 0

def test_host_unit_tests():
    """Runs the C++ host unit tests (doctest)."""
    run_script("test/run_host_tests.sh")

def test_differential_rounding():
    """Runs the C++ differential rounding tests."""
    run_script("test/run_differential_rounding_test.sh")

def test_hyphenation_eval():
    """Runs the C++ hyphenation evaluation."""
    run_script("test/run_hyphenation_eval.sh")

def test_host_server_smoke():
    """Runs the C++ host server smoke test (curl-based)."""
    run_script("test/run_host_server.sh")
