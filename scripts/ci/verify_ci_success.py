#!/usr/bin/env python3
"""Verify that the ci.yml workflow succeeded for a specific exact commit SHA.

Environment variables (required):
  GITHUB_REPOSITORY  - owner/repo
  GH_TOKEN           - GitHub token for API access
  GITHUB_SHA         - commit SHA to match
"""

import json
import os
import sys
import urllib.parse
import urllib.request
from typing import Dict, Any


def _api_get(url: str, token: str) -> dict:
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def check_runs(payload: Dict[str, Any], expected_sha: str) -> bool:
    """Evaluate API payload for a successful push run matching the SHA.

    Returns True if at least one run matches all criteria.
    """
    for run in payload.get("workflow_runs", []):
        # We only accept runs that exactly match the expected head_sha.
        if run.get("head_sha") != expected_sha:
            continue
        # Pull-request and manually-dispatched runs are not authoritative for
        # delivery. Only the push run exercises the exact release gate.
        if run.get("event") != "push":
            continue
        # We only accept fully completed runs.
        if run.get("status") != "completed":
            continue
        # We only accept successful conclusions.
        if run.get("conclusion") == "success":
            return True
    return False


def main() -> None:
    repo = os.environ["GITHUB_REPOSITORY"]
    token = os.environ["GH_TOKEN"]
    sha = os.environ["GITHUB_SHA"]

    url = (
        f"https://api.github.com/repos/{repo}/actions/workflows/ci.yml"
        f"/runs?event=push&head_sha={urllib.parse.quote(sha)}&per_page=100"
    )

    try:
        payload = _api_get(url, token)
    except Exception as e:
        print(f"::error::Failed to query GitHub API: {e}", file=sys.stderr)
        sys.exit(1)

    if check_runs(payload, sha):
        print(f"Success: A completed successful push CI run exists for {sha}.")
        sys.exit(0)
    else:
        print(
            f"::error::No completed successful push ci.yml run found for exact SHA {sha}.",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
