import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from scripts.verify_terminus_device import evidence_failures


def evidence(**overrides):
    value = {
        "timer_wake_count": 10,
        "completed_cycle_count": 10,
        "wifi_success_count": 10,
        "fetch_success_count": 10,
        "render_success_count": 10,
        "verification_target_completed_cycle_count": 0,
        "verification_rendezvous_count": 4,
        "last_cycle_complete": True,
        "last_wifi_started": True,
        "last_wifi_connected": True,
        "last_fetch_attempted": True,
        "last_fetch_ok": True,
        "last_used_stale_image": False,
        "last_render_completed": True,
        "last_rearm_armed": True,
        "last_rearm_interval_seconds": 210,
        "last_server_refresh_seconds": 210,
    }
    value.update(overrides)
    return value


def test_two_complete_cycles_pass():
    before = evidence(
        timer_wake_count=8,
        completed_cycle_count=8,
        wifi_success_count=8,
        fetch_success_count=8,
        render_success_count=8,
        verification_rendezvous_count=3,
    )
    assert evidence_failures(before, evidence(), 2) == []


def test_stale_fallback_and_missing_fetch_fail():
    before = evidence(
        timer_wake_count=8,
        completed_cycle_count=8,
        wifi_success_count=8,
        fetch_success_count=8,
        render_success_count=8,
        verification_rendezvous_count=3,
    )
    after = evidence(
        fetch_success_count=9, last_fetch_ok=False, last_used_stale_image=True
    )
    failures = evidence_failures(before, after, 2)
    assert any("fetch_success_count" in failure for failure in failures)
    assert "last_fetch_ok is not true" in failures
    assert "last cycle used the stale-image fallback" in failures


def test_one_wake_cannot_prove_rearm():
    before = evidence(
        timer_wake_count=9,
        completed_cycle_count=9,
        wifi_success_count=9,
        fetch_success_count=9,
        render_success_count=9,
        verification_rendezvous_count=3,
    )
    failures = evidence_failures(before, evidence(), 2)
    assert any("only 1 timer wakes" in failure for failure in failures)


def test_dynamic_host_cadence_does_not_override_same_cycle_device_evidence():
    before = evidence(
        timer_wake_count=8,
        completed_cycle_count=8,
        wifi_success_count=8,
        fetch_success_count=8,
        render_success_count=8,
        verification_rendezvous_count=3,
    )
    after = evidence(last_rearm_interval_seconds=900, last_server_refresh_seconds=900)
    assert evidence_failures(before, after, 2) == []


def test_mismatched_same_cycle_cadence_fails():
    before = evidence(
        timer_wake_count=8,
        completed_cycle_count=8,
        wifi_success_count=8,
        fetch_success_count=8,
        render_success_count=8,
        verification_rendezvous_count=3,
    )
    after = evidence(last_rearm_interval_seconds=900, last_server_refresh_seconds=210)
    failures = evidence_failures(before, after, 2)
    assert any("same-cycle server interval" in failure for failure in failures)
