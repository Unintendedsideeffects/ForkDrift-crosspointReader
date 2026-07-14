import pytest
import sys
import os

# Add scripts directory to path to import verify_ci_success
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from scripts.ci.verify_ci_success import check_runs

def test_check_runs_success():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "success"}
        ]
    }
    assert check_runs(payload, "abc1234") is True

def test_check_runs_failure():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "failure"}
        ]
    }
    assert check_runs(payload, "abc1234") is False

def test_check_runs_cancelled():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "cancelled"}
        ]
    }
    assert check_runs(payload, "abc1234") is False

def test_check_runs_in_progress():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "in_progress", "conclusion": None}
        ]
    }
    assert check_runs(payload, "abc1234") is False

def test_check_runs_wrong_sha():
    payload = {
        "workflow_runs": [
            {"head_sha": "def5678", "event": "push", "status": "completed", "conclusion": "success"}
        ]
    }
    assert check_runs(payload, "abc1234") is False

def test_check_runs_empty_results():
    payload = {"workflow_runs": []}
    assert check_runs(payload, "abc1234") is False

def test_check_runs_multiple_runs():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "failure"},
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "success"},
        ]
    }
    assert check_runs(payload, "abc1234") is True


def test_check_runs_rejects_successful_pull_request_run():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "pull_request", "status": "completed", "conclusion": "success"}
        ]
    }
    assert check_runs(payload, "abc1234") is False


def test_check_runs_does_not_mask_failed_push_with_successful_pull_request():
    payload = {
        "workflow_runs": [
            {"head_sha": "abc1234", "event": "push", "status": "completed", "conclusion": "failure"},
            {"head_sha": "abc1234", "event": "pull_request", "status": "completed", "conclusion": "success"},
        ]
    }
    assert check_runs(payload, "abc1234") is False
