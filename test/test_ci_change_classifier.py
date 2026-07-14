import pytest
import sys
from pathlib import Path

# Add scripts directory to path to import the module
sys.path.insert(0, str(Path(__file__).parent.parent / 'scripts' / 'ci'))

from classify_changes import classify_changes, classify_path

def test_no_paths():
    assert classify_changes([]) == (True, True, True)

def test_force_all():
    assert classify_changes(['docs/readme.md'], force_all=True) == (True, True, True)

def test_rule_2_ignores():
    # docs/, plans/, test/, .github/, README/BUGS/TODO
    assert classify_path('docs/api.md') == (False, False, False)
    assert classify_path('plans/arch.md') == (False, False, False)
    assert classify_path('test/test_ci.py') == (False, False, False)
    assert classify_path('.github/ISSUE_TEMPLATE.md') == (False, False, False)
    assert classify_path('README.md') == (False, False, False)
    assert classify_path('BUGS.md') == (False, False, False)
    assert classify_path('TODO.md') == (False, False, False)
    assert classify_changes(['docs/api.md', 'README.md']) == (False, False, False)


def test_epub_fixture_changes_run_simulator_smoke():
    assert classify_path('test/epubs/test_supsub.epub') == (False, True, False)

def test_rule_3_pipeline():
    assert classify_path('.github/workflows/ci.yml') == (True, True, True)
    assert classify_path('.github/workflows/build.yml') == (True, True, True)

def test_rule_4_screen_harness():
    assert classify_path('tools/screen-harness/test.py') == (False, True, False)

def test_rule_5_firmware_inputs():
    assert classify_path('src/main.cpp') == (True, True, True)
    assert classify_path('lib/foo.c') == (True, True, True)
    assert classify_path('include/foo.h') == (True, True, True)
    assert classify_path('open-x4-sdk/sdk.c') == (True, True, True)
    assert classify_path('scripts/build.py') == (True, True, True)
    assert classify_path('platformio.ini') == (True, True, True)
    assert classify_path('partitions.csv') == (True, True, True)
    assert classify_path('pyproject.toml') == (True, True, True)
    assert classify_path('uv.lock') == (True, True, True)
    assert classify_path('.python-version') == (True, True, True)

def test_rule_6_failsafe():
    assert classify_path('unknown_file.txt') == (True, True, True)
    assert classify_path('tools/unknown/script.py') == (True, True, True)

def test_mixed_unions():
    # docs + screen-harness -> simulator only
    assert classify_changes(['docs/api.md', 'tools/screen-harness/test.py']) == (False, True, False)
    # docs + source -> all true
    assert classify_changes(['docs/api.md', 'src/main.cpp']) == (True, True, True)
    # unknown -> all true
    assert classify_changes(['docs/api.md', 'unknown_dir/file.txt']) == (True, True, True)
