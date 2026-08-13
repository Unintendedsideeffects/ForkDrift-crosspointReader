#!/usr/bin/env python3
"""
Feature Manifest Reader and Validator for CrossPoint.

Loads config/features.yaml and provides manifest validation, feature resolution,
and profile calculation.
"""

from dataclasses import dataclass, field
from pathlib import Path
from types import MappingProxyType
from typing import Any, Dict, List, Mapping, Optional, Set, Tuple
import yaml

ROOT = Path(__file__).parent.parent
MANIFEST_PATH = ROOT / "config" / "features.yaml"


@dataclass(frozen=True)
class FeatureRecord:
    key: str
    macro: str
    label: str
    description: str
    estimated_size_kib: int
    implemented: bool = True
    stable: bool = True
    requires_all: Tuple[str, ...] = ()
    requires_any: Tuple[str, ...] = ()
    conflicts: Tuple[str, ...] = ()
    recommends: Tuple[str, ...] = ()


@dataclass(frozen=True)
class ProfileRecord:
    name: str
    description: str
    extends: Optional[str] = None
    features: Mapping[str, bool] = field(default_factory=lambda: MappingProxyType({}))


class FeatureManifest:
    def __init__(self, manifest_dict: Dict[str, Any]):
        self._raw = manifest_dict
        self.schema_version = manifest_dict.get("schema_version") if isinstance(manifest_dict, dict) else None
        self._features: Dict[str, FeatureRecord] = {}
        self._profiles: Dict[str, ProfileRecord] = {}

        self._parse_and_validate()

    @property
    def features(self) -> Mapping[str, FeatureRecord]:
        return MappingProxyType(self._features)

    @property
    def profiles(self) -> Mapping[str, ProfileRecord]:
        return MappingProxyType(self._profiles)

    def _parse_and_validate(self):
        if not isinstance(self._raw, dict):
            raise ValueError("Manifest root must be a mapping")

        if self.schema_version != 1:
            raise ValueError(f"Unsupported schema_version: {self.schema_version}")

        # Parse features
        if "features" not in self._raw or not isinstance(self._raw["features"], dict):
            raise ValueError("Manifest missing features mapping")
        raw_features = self._raw["features"]
        macros_seen: Set[str] = set()

        for key, data in raw_features.items():
            if not isinstance(data, dict):
                raise ValueError(f"Feature {key} must be a mapping")
            if key in self._features:
                raise ValueError(f"Duplicate feature key: {key}")

            macro = data.get("macro")
            if not isinstance(macro, str) or not macro:
                raise ValueError(f"Feature {key} missing or empty macro string")
            if macro in macros_seen:
                raise ValueError(f"Duplicate macro {macro} in feature {key}")
            macros_seen.add(macro)

            label = data.get("label")
            if not isinstance(label, str) or not label:
                raise ValueError(f"Feature {key} missing or empty label string")

            description = data.get("description")
            if not isinstance(description, str) or not description:
                raise ValueError(f"Feature {key} missing or empty description string")

            implemented = data.get("implemented")
            if not isinstance(implemented, bool):
                raise ValueError(f"Feature {key} missing or non-boolean implemented field")

            stable = data.get("stable")
            if not isinstance(stable, bool):
                raise ValueError(f"Feature {key} missing or non-boolean stable field")

            estimated_size_kib = data.get("estimated_size_kib")
            if not isinstance(estimated_size_kib, int) or type(estimated_size_kib) is bool or estimated_size_kib < 0:
                raise ValueError(f"Feature {key} missing or invalid non-negative integer estimated_size_kib")

            def check_list_str(field_name: str) -> Tuple[str, ...]:
                val = data.get(field_name)
                if not isinstance(val, list) or not all(isinstance(x, str) for x in val):
                    raise ValueError(f"Feature {key} field {field_name} must be a list of strings")
                return tuple(val)

            requires_all = check_list_str("requires_all")
            requires_any = check_list_str("requires_any")
            conflicts = check_list_str("conflicts")
            recommends = check_list_str("recommends")

            feat = FeatureRecord(
                key=key,
                macro=macro,
                label=label,
                description=description,
                estimated_size_kib=estimated_size_kib,
                implemented=implemented,
                stable=stable,
                requires_all=requires_all,
                requires_any=requires_any,
                conflicts=conflicts,
                recommends=recommends,
            )
            self._features[key] = feat

        # Validate feature dependencies and references
        for key, feat in self._features.items():
            for req in feat.requires_all:
                if req not in self._features:
                    raise ValueError(f"Feature {key} requires unknown feature {req}")
                if req == key:
                    raise ValueError(f"Feature {key} requires itself")
            for req in feat.requires_any:
                if req not in self._features:
                    raise ValueError(f"Feature {key} requires_any unknown feature {req}")
                if req == key:
                    raise ValueError(f"Feature {key} requires_any itself")
            for conf in feat.conflicts:
                if conf not in self._features:
                    raise ValueError(f"Feature {key} conflicts with unknown feature {conf}")
                if conf == key:
                    raise ValueError(f"Feature {key} conflicts with itself")
            for rec in feat.recommends:
                if rec not in self._features:
                    raise ValueError(f"Feature {key} recommends unknown feature {rec}")

        # Cycle detection on requires_all graph using 3-color (White/Gray/Black) DFS
        WHITE, GRAY, BLACK = 0, 1, 2
        color: Dict[str, int] = {k: WHITE for k in self._features}

        def dfs(u: str):
            color[u] = GRAY
            for v in self._features[u].requires_all:
                if color[v] == GRAY:
                    raise ValueError(f"Dependency cycle detected involving feature {v}")
                elif color[v] == WHITE:
                    dfs(v)
            color[u] = BLACK

        for k in self._features:
            if color[k] == WHITE:
                dfs(k)

        # Parse profiles
        if "profiles" not in self._raw or not isinstance(self._raw["profiles"], dict):
            raise ValueError("Manifest missing profiles mapping")
        raw_profiles = self._raw["profiles"]

        for pname, pdata in raw_profiles.items():
            if not isinstance(pdata, dict):
                raise ValueError(f"Profile {pname} must be a mapping")

            description = pdata.get("description")
            if not isinstance(description, str) or not description:
                raise ValueError(f"Profile {pname} missing or empty description string")

            extends = pdata.get("extends")
            if extends is not None and not isinstance(extends, str):
                raise ValueError(f"Profile {pname} extends field must be a string or None")

            p_feats = pdata.get("features")
            if not isinstance(p_feats, dict):
                raise ValueError(f"Profile {pname} missing features mapping")

            for fk, fval in p_feats.items():
                if not isinstance(fk, str) or fk not in self._features:
                    raise ValueError(f"Profile {pname} references unknown feature {fk}")
                if not isinstance(fval, bool):
                    raise ValueError(f"Profile {pname} feature {fk} value must be a boolean")

            prof = ProfileRecord(
                name=pname,
                description=description,
                extends=extends,
                features=MappingProxyType(dict(p_feats)),
            )
            self._profiles[pname] = prof

        # Validate profile inheritance cycles
        for pname, prof in self._profiles.items():
            visited = set()
            curr = prof.extends
            while curr:
                if curr in visited or curr == pname:
                    raise ValueError(f"Profile inheritance cycle detected in profile {pname}")
                visited.add(curr)
                if curr not in self._profiles:
                    raise ValueError(f"Profile {pname} extends unknown profile {curr}")
                curr = self._profiles[curr].extends

    def resolve_profile(self, name: str) -> Dict[str, bool]:
        if name not in self._profiles:
            raise ValueError(f"Unknown profile: {name}")

        profile_chain = []
        curr: Optional[str] = name
        while curr:
            profile_chain.append(self._profiles[curr])
            curr = self._profiles[curr].extends

        # Merge features from base down to leaf
        base_overrides: Dict[str, bool] = {}
        for p in reversed(profile_chain):
            base_overrides.update(p.features)

        return self.resolve_features(base_overrides)

    def resolve_features(self, overrides: Optional[Dict[str, bool]] = None) -> Dict[str, bool]:
        resolved: Dict[str, bool] = {key: False for key in self._features}
        if overrides:
            for k, v in overrides.items():
                if k not in self._features:
                    raise ValueError(f"Unknown feature override: {k}")
                if not isinstance(v, bool):
                    raise ValueError(f"Feature override {k} must be a boolean")
                resolved[k] = v

        # Deterministically close requires_all dependencies
        changed = True
        while changed:
            changed = False
            for k, enabled in list(resolved.items()):
                if not enabled:
                    continue
                feat = self._features[k]
                for req in feat.requires_all:
                    if not resolved.get(req, False):
                        resolved[req] = True
                        changed = True

        # Validate requires_any semantics
        for k, enabled in resolved.items():
            if not enabled:
                continue
            feat = self._features[k]
            if feat.requires_any:
                if not any(resolved.get(r, False) for r in feat.requires_any):
                    raise ValueError(f"Feature {k} requires at least one of: {list(feat.requires_any)}")

        # Validate conflict semantics
        for k, enabled in resolved.items():
            if not enabled:
                continue
            feat = self._features[k]
            for conf in feat.conflicts:
                if resolved.get(conf, False):
                    raise ValueError(f"Feature {k} conflicts with {conf}")

        return resolved


def load_manifest(path: Path = MANIFEST_PATH) -> FeatureManifest:
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    return FeatureManifest(data)
