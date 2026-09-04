#!/usr/bin/env python3
"""Fail-closed GHCR/GitHub release guards and compensating tag cleanup."""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

from release_common import ReleaseError, parse_version

MAX_API_BYTES = 4 * 1024 * 1024
MAX_VERSION_PAGES = 100


class GitHubApi:
    def __init__(self, token: str, api_url: str = "https://api.github.com") -> None:
        if not token:
            raise ReleaseError("GITHUB_TOKEN is required")
        self.token = token
        self.api_url = api_url.rstrip("/")

    def request(self, method: str, path: str, allow_missing: bool = False):
        request = urllib.request.Request(
            f"{self.api_url}{path}", method=method,
            headers={"Accept": "application/vnd.github+json",
                     "Authorization": f"Bearer {self.token}",
                     "X-GitHub-Api-Version": "2026-03-10",
                     "User-Agent": "graphx-release-guard/1"})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                payload = response.read(MAX_API_BYTES + 1)
                if len(payload) > MAX_API_BYTES:
                    raise ReleaseError("GitHub API response exceeds 4 MiB")
                if not payload:
                    return None
                return json.loads(payload)
        except urllib.error.HTTPError as error:
            if allow_missing and error.code == 404:
                return None
            raise ReleaseError(f"GitHub API {method} {path} failed with HTTP {error.code}") from error
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
            raise ReleaseError(f"GitHub API {method} {path} failed: {error}") from error


def package_base(owner: str, owner_type: str, package: str) -> str:
    if owner_type == "Organization":
        prefix = f"/orgs/{urllib.parse.quote(owner, safe='')}"
    elif owner_type == "User":
        prefix = f"/users/{urllib.parse.quote(owner, safe='')}"
    else:
        raise ReleaseError("repository owner type must be Organization or User")
    return (f"{prefix}/packages/container/"
            f"{urllib.parse.quote(package, safe='')}/versions")


def list_versions(api: GitHubApi, owner: str, owner_type: str, package: str) -> list[dict]:
    base = package_base(owner, owner_type, package)
    versions: list[dict] = []
    for page in range(1, MAX_VERSION_PAGES + 1):
        document = api.request("GET", f"{base}?per_page=100&page={page}", allow_missing=True)
        if document is None:
            return []
        if not isinstance(document, list) or not all(isinstance(item, dict) for item in document):
            raise ReleaseError("GitHub package versions response has an invalid shape")
        versions.extend(document)
        if len(document) < 100:
            return versions
    raise ReleaseError("GitHub package version search exceeded 10000 entries")


def tagged_versions(versions: list[dict], tag: str) -> list[dict]:
    matches: list[dict] = []
    for version in versions:
        metadata = version.get("metadata")
        container = metadata.get("container") if isinstance(metadata, dict) else None
        tags = container.get("tags") if isinstance(container, dict) else None
        if not isinstance(tags, list) or not all(isinstance(value, str) for value in tags):
            raise ReleaseError("GitHub package version has invalid container tag metadata")
        if tag in tags:
            matches.append(version)
    return matches


def numeric_version(tag: str) -> str:
    version = parse_version(tag[1:] if tag.startswith("v") else tag)
    if tag not in (version, f"v{version}"):
        raise ReleaseError("tag must be a canonical version with an optional v prefix")
    return version


def assert_absent(api: GitHubApi, repository: str, owner: str, owner_type: str,
                  tag: str, packages: list[str]) -> None:
    tag = numeric_version(tag)
    release_tag = f"v{tag}"
    if api.request("GET", f"/repos/{repository}/releases/tags/{release_tag}",
                   allow_missing=True) is not None:
        raise ReleaseError(f"GitHub release {release_tag} already exists")
    for package in packages:
        if tagged_versions(list_versions(api, owner, owner_type, package), tag):
            raise ReleaseError(f"GHCR tag {package}:{tag} already exists")


def delete_tag(api: GitHubApi, owner: str, owner_type: str, package: str,
               tag: str, companion_prefix: str, allow_absent: bool = False) -> None:
    tag = numeric_version(tag)
    matches = tagged_versions(list_versions(api, owner, owner_type, package), tag)
    if not matches and allow_absent:
        return
    if len(matches) != 1:
        raise ReleaseError(f"expected exactly one GHCR version for cleanup of {package}:{tag}")
    version = matches[0]
    identifier = version.get("id")
    metadata = version["metadata"]["container"]
    tags = metadata["tags"]
    if isinstance(identifier, bool) or not isinstance(identifier, int):
        raise ReleaseError("GHCR package version has no numeric ID")
    unexpected = [value for value in tags if value != tag and not value.startswith(companion_prefix)]
    if unexpected:
        raise ReleaseError(f"refusing to delete {package}:{tag}; version has unrelated tags")
    api.request("DELETE", f"{package_base(owner, owner_type, package)}/{identifier}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("assert-absent", "delete-tag"))
    parser.add_argument("--repository", required=True)
    parser.add_argument("--owner", required=True)
    parser.add_argument("--owner-type", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--package", action="append", required=True)
    parser.add_argument("--companion-prefix", default="staging-")
    parser.add_argument("--allow-absent", action="store_true")
    args = parser.parse_args()
    if "/" not in args.repository or any(not value for value in args.repository.split("/", 1)):
        raise ReleaseError("repository must be owner/name")
    numeric_version(args.tag)
    api = GitHubApi(os.environ.get("GITHUB_TOKEN", ""), os.environ.get("GITHUB_API_URL",
                                                                       "https://api.github.com"))
    if args.action == "assert-absent":
        assert_absent(api, args.repository, args.owner, args.owner_type, args.tag, args.package)
    else:
        if len(args.package) != 1:
            raise ReleaseError("delete-tag accepts exactly one --package")
        delete_tag(api, args.owner, args.owner_type, args.package[0], args.tag,
                   args.companion_prefix, args.allow_absent)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ReleaseError, OSError, ValueError) as error:
        print(f"graphx-release-guard: {error}", file=sys.stderr)
        sys.exit(2)
