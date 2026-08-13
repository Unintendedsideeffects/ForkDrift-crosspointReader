import sys
import subprocess
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from scripts.feature_manifest import load_manifest, FeatureManifest, FeatureRecord, ProfileRecord
from scripts.generate_build_config import FEATURES, FEATURE_METADATA, PROFILES


def test_independent_features_comparison():
    manifest = load_manifest()
    assert set(manifest.features.keys()) == set(FEATURES.keys())

    for key, feat in FEATURES.items():
        m_feat = manifest.features[key]
        meta = FEATURE_METADATA.get(key)
        assert m_feat.key == key
        assert m_feat.macro == feat.flag
        assert m_feat.label == feat.name
        assert m_feat.description == feat.description
        assert m_feat.estimated_size_kib == feat.size_kb
        if meta:
            assert m_feat.implemented == meta.implemented
            assert m_feat.stable == meta.stable
            assert list(m_feat.requires_all) == meta.requires
            assert list(m_feat.requires_any) == meta.requires_any
            assert list(m_feat.conflicts) == meta.conflicts
            assert list(m_feat.recommends) == meta.recommends
        else:
            assert m_feat.implemented is True
            assert m_feat.stable is True
            assert list(m_feat.requires_all) == []
            assert list(m_feat.requires_any) == []
            assert list(m_feat.conflicts) == []
            assert list(m_feat.recommends) == []


def test_raw_profile_equivalence():
    manifest = load_manifest()
    assert set(manifest.profiles.keys()) == set(PROFILES.keys())

    for pname, prof in PROFILES.items():
        m_prof = manifest.profiles[pname]
        assert m_prof.name == pname
        assert m_prof.description == prof["description"]
        assert dict(m_prof.features) == prof["features"]


def test_true_immutability():
    manifest = load_manifest()
    feat = manifest.features["bookerly_fonts"]
    prof = manifest.profiles["standard"]

    # Tuple checks
    assert isinstance(feat.requires_all, tuple)
    assert isinstance(feat.requires_any, tuple)
    assert isinstance(feat.conflicts, tuple)
    assert isinstance(feat.recommends, tuple)

    # Read-only mapping checks
    with pytest.raises(TypeError):
        manifest.features["new_key"] = feat  # type: ignore

    with pytest.raises(TypeError):
        manifest.profiles["new_prof"] = prof  # type: ignore

    with pytest.raises(TypeError):
        prof.features["bookerly_fonts"] = False  # type: ignore


def test_diamond_graph_acceptance():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "A": {"macro": "ENABLE_A", "label": "A", "description": "A", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["B", "C"], "requires_any": [], "conflicts": [], "recommends": []},
            "B": {"macro": "ENABLE_B", "label": "B", "description": "B", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["D"], "requires_any": [], "conflicts": [], "recommends": []},
            "C": {"macro": "ENABLE_C", "label": "C", "description": "C", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["D"], "requires_any": [], "conflicts": [], "recommends": []},
            "D": {"macro": "ENABLE_D", "label": "D", "description": "D", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    manifest = FeatureManifest(manifest_dict)
    resolved = manifest.resolve_features({"A": True})
    assert resolved == {"A": True, "B": True, "C": True, "D": True}


def test_unknown_override_rejection():
    manifest = load_manifest()
    with pytest.raises(ValueError, match="Unknown feature override: invalid_feature_name"):
        manifest.resolve_features({"invalid_feature_name": True})


def test_non_boolean_override_rejection():
    manifest = load_manifest()
    with pytest.raises(ValueError, match="Feature override bookerly_fonts must be a boolean"):
        manifest.resolve_features({"bookerly_fonts": "false"})


def test_requires_all_closure():
    manifest = load_manifest()
    # opds requires_all calibre_sync, calibre_sync requires_all integrations
    resolved = manifest.resolve_features({"opds": True})
    assert resolved["opds"] is True
    assert resolved["calibre_sync"] is True
    assert resolved["integrations"] is True


def test_unsatisfied_requires_any():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "opt": {"macro": "ENABLE_OPT", "label": "Opt", "description": "Opt", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": ["f1", "f2"], "conflicts": [], "recommends": []},
            "f1": {"macro": "ENABLE_F1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
            "f2": {"macro": "ENABLE_F2", "label": "F2", "description": "F2", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    manifest = FeatureManifest(manifest_dict)
    with pytest.raises(ValueError, match="requires at least one of"):
        manifest.resolve_features({"opt": True})

    # Satisfied case
    resolved = manifest.resolve_features({"opt": True, "f1": True})
    assert resolved["opt"] is True
    assert resolved["f1"] is True


def test_conflicts_rejection():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "f1": {"macro": "ENABLE_F1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": ["f2"], "recommends": []},
            "f2": {"macro": "ENABLE_F2", "label": "F2", "description": "F2", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    manifest = FeatureManifest(manifest_dict)
    with pytest.raises(ValueError, match="conflicts with"):
        manifest.resolve_features({"f1": True, "f2": True})


def test_checker_detects_mutated_implemented():
    manifest_path = Path(__file__).parent.parent / "config" / "features.yaml"
    original_content = manifest_path.read_text()
    try:
        mutated_content = original_content.replace("implemented: true", "implemented: false", 1)
        manifest_path.write_text(mutated_content)

        repo_root = Path(__file__).parent.parent
        cmd = [sys.executable, str(repo_root / "scripts" / "check_feature_key_sync.py")]
        res = subprocess.run(cmd, cwd=repo_root, capture_output=True, text=True)
        assert res.returncode == 1
        assert "implemented mismatch" in res.stdout or "Feature synchronization check failed" in res.stdout
    finally:
        manifest_path.write_text(original_content)


def test_negative_structural_contract_missing_label():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {
                "macro": "ENABLE_FEAT1",
                # missing label
                "description": "Feat 1",
                "estimated_size_kib": 10,
                "implemented": True,
                "stable": True,
                "requires_all": [],
                "requires_any": [],
                "conflicts": [],
                "recommends": []
            }
        },
        "profiles": {}
    }
    with pytest.raises(ValueError, match="missing or empty label string"):
        FeatureManifest(manifest_dict)


def test_negative_structural_contract_non_boolean_profile_value():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {
                "macro": "ENABLE_FEAT1",
                "label": "Feat 1",
                "description": "Feat 1",
                "estimated_size_kib": 10,
                "implemented": True,
                "stable": True,
                "requires_all": [],
                "requires_any": [],
                "conflicts": [],
                "recommends": []
            }
        },
        "profiles": {
            "prof1": {
                "description": "Profile 1",
                "features": {"feat1": "yes"}  # non-boolean value
            }
        }
    }
    with pytest.raises(ValueError, match="value must be a boolean"):
        FeatureManifest(manifest_dict)


def test_duplicate_macro():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {"macro": "ENABLE_FEAT1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
            "feat2": {"macro": "ENABLE_FEAT1", "label": "F2", "description": "F2", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    with pytest.raises(ValueError, match="Duplicate macro ENABLE_FEAT1"):
        FeatureManifest(manifest_dict)


def test_unknown_dependency():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {"macro": "ENABLE_FEAT1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["unknown_feat"], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    with pytest.raises(ValueError, match="requires unknown feature unknown_feat"):
        FeatureManifest(manifest_dict)


def test_dependency_cycle():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {"macro": "ENABLE_FEAT1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["feat2"], "requires_any": [], "conflicts": [], "recommends": []},
            "feat2": {"macro": "ENABLE_FEAT2", "label": "F2", "description": "F2", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": ["feat1"], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {}
    }
    with pytest.raises(ValueError, match="Dependency cycle detected"):
        FeatureManifest(manifest_dict)


def test_unknown_profile_feature():
    manifest_dict = {
        "schema_version": 1,
        "features": {
            "feat1": {"macro": "ENABLE_FEAT1", "label": "F1", "description": "F1", "estimated_size_kib": 0, "implemented": True, "stable": True, "requires_all": [], "requires_any": [], "conflicts": [], "recommends": []},
        },
        "profiles": {
            "prof1": {"description": "Prof 1", "features": {"unknown_feat": True}}
        }
    }
    with pytest.raises(ValueError, match="references unknown feature unknown_feat"):
        FeatureManifest(manifest_dict)


def test_deterministic_resolution():
    manifest = load_manifest()
    res1 = manifest.resolve_profile("standard")
    res2 = manifest.resolve_profile("standard")
    assert res1 == res2


def test_invalid_schema_version():
    manifest_dict = {
        "schema_version": 99,
        "features": {},
        "profiles": {}
    }
    with pytest.raises(ValueError, match="Unsupported schema_version: 99"):
        FeatureManifest(manifest_dict)
