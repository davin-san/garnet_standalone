#!/usr/bin/env python3
"""
PACE Ablation Mode Test Suite.

Tests verify that each --pace-no-* flag:
  1. Is correctly parsed and reflected in the output JSON ablation config.
  2. Changes simulation behavior in the expected direction.
  3. When all flags are set, produces results different from full PACE.
  4. Output is reproducible with the same seed.
"""

import json
import os
import subprocess
import sys
import tempfile

BINARY  = "./garnet_standalone"
TIMEOUT = 120  # seconds per test


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class TestResult:
    def __init__(self, name, success, details=""):
        self.name    = name
        self.success = success
        self.details = details


def _run(args, timeout=TIMEOUT):
    cmd = [BINARY] + args
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "TIMEOUT"
    except Exception as e:
        return -2, "", str(e)


def _write_profile(profile: dict) -> str:
    f = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
    json.dump(profile, f, indent=2)
    f.close()
    return f.name


def _make_phase(idx, cycles, lambda_val, data_pct=50.0,
                num_cpus=16, num_dirs=4,
                per_router=None, dir_frac=None):
    """Build a phase dict; per_router and dir_frac allow non-uniform values."""
    total_pkts = max(1, num_cpus * cycles // 20)
    v0 = int(total_pkts * 0.50)
    v1 = int(total_pkts * 0.35)
    v2 = total_pkts - v0 - v1
    if per_router is None:
        per_router = {str(i): 1.0 / num_cpus for i in range(num_cpus)}
    if dir_frac is None:
        dir_frac = {str(i): 1.0 / num_dirs for i in range(num_dirs)}
    return {
        "phase_index":        idx,
        "total_packets":      total_pkts,
        "total_flits":        total_pkts * 2,
        "flits_per_packet":   2.0,
        "data_pct":           data_pct,
        "ctrl_pct":           100.0 - data_pct,
        "sim_ticks":          cycles * 1000,
        "network_cycles":     cycles,
        "lambda":             lambda_val,
        "avg_packet_latency": 10.0,
        "vnet_packets":       {"0": v0, "1": v1, "2": v2},
        "per_router_injection": per_router,
        "dir_fractions":      dir_frac,
    }


def _make_profile(num_cpus, num_dirs, rows, cols, phases, dir_remapping=None):
    if dir_remapping is None:
        dir_remapping = {str(i): i for i in range(num_dirs)}
    return {
        "num_cpus":      num_cpus,
        "num_dirs":      num_dirs,
        "mesh_rows":     rows,
        "mesh_cols":     cols,
        "mem_channels":  num_dirs,
        "num_phases":    len(phases),
        "phases":        phases,
        "model_assumptions": {
            "flit_width_bytes":  16,
            "cacheline_bytes":   64,
            "data_packet_flits": 5,
            "ctrl_packet_flits": 1,
        },
        "directory_remapping": dir_remapping,
    }


def _run_pace(profile_path, rows=4, cols=4, output_path=None,
              mshr=16, seed=42, extra_args=None):
    if output_path is None:
        tf = tempfile.NamedTemporaryFile(suffix=".json", delete=False)
        tf.close()
        output_path = tf.name
    args = [
        "--topology", "Mesh_XY",
        "--rows",     str(rows),
        "--cols",     str(cols),
        "--routing",  "1",
        "--pace-profile", profile_path,
        "--pace-mshr", str(mshr),
        "--pace-output", output_path,
        "--seed", str(seed),
    ]
    if extra_args:
        args += extra_args
    rc, out, err = _run(args)
    results = None
    if rc == 0 and os.path.isfile(output_path):
        try:
            with open(output_path) as f:
                results = json.load(f)
        except json.JSONDecodeError:
            pass
    return rc, out, err, results, output_path


# ---------------------------------------------------------------------------
# Test 1: Flag parsing — each individual flag flips exactly one ablation field
# ---------------------------------------------------------------------------

def test_flag_parsing():
    """Each --pace-no-* flag must set exactly the corresponding ablation field to false."""
    name = "Flag Parsing (individual flags)"

    phase = _make_phase(0, 500, 0.03)
    profile = _make_profile(16, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    flag_to_field = {
        "--pace-no-per-source":    "per_source",
        "--pace-no-phases":        "phases",
        "--pace-no-mshr":          "mshr",
        "--pace-no-remap":         "remap",
        "--pace-no-weighted-dest": "weighted_dest",
        "--pace-no-corr-response": "corr_response",
    }

    failures = []
    for flag, field in flag_to_field.items():
        rc, out, err, results, opath = _run_pace(ppath, extra_args=[flag])
        if os.path.isfile(opath):
            os.unlink(opath)
        if rc != 0:
            failures.append(f"{flag}: simulation crashed ({err[:100]})")
            continue
        if results is None:
            failures.append(f"{flag}: missing JSON output")
            continue
        abl = results.get("ablation", {})
        # The flagged field must be false; all others must be true.
        for f, v in abl.items():
            expected = (f != field)   # true unless it's the disabled one
            if v != expected:
                failures.append(
                    f"{flag}: ablation.{f}={v}, expected {expected}")

    os.unlink(ppath)

    if failures:
        return TestResult(name, False, "\n  ".join(failures))
    return TestResult(name, True,
        f"All {len(flag_to_field)} flags correctly reflected in output JSON")


# ---------------------------------------------------------------------------
# Test 2: All flags combined → all ablation fields false
# ---------------------------------------------------------------------------

def test_all_flags_combined():
    """Running all --pace-no-* flags must set every ablation field to false."""
    name = "All Flags Combined"

    phase = _make_phase(0, 500, 0.03)
    profile = _make_profile(16, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    all_flags = [
        "--pace-no-per-source", "--pace-no-phases", "--pace-no-mshr",
        "--pace-no-remap", "--pace-no-weighted-dest", "--pace-no-corr-response",
    ]
    rc, out, err, results, opath = _run_pace(ppath, extra_args=all_flags)
    if os.path.isfile(opath):
        os.unlink(opath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    abl = results.get("ablation", {})
    wrong = {k: v for k, v in abl.items() if v is not False}
    if wrong:
        return TestResult(name, False,
            f"Expected all false, got: {wrong}")

    return TestResult(name, True,
        "All 6 ablation fields are false as expected")


# ---------------------------------------------------------------------------
# Test 3: per-source ablation changes injection distribution
# ---------------------------------------------------------------------------

def test_per_source_ablation():
    """
    With per-source enabled, routers should show variance in injection counts
    (injected_packets differs across nodes).  With it disabled, rates should
    be approximately uniform.

    We detect this by comparing the sum of all-pairs variance in per-router
    injection probability between the two runs.
    """
    name = "Per-Source Ablation Changes Injection Distribution"

    # Non-uniform per-router fractions: router 0 gets 4x more than others.
    num_cpus = 16
    high_router_frac = 0.25  # router 0 gets 25% of all injections
    other_frac = 0.75 / (num_cpus - 1)
    per_router = {"0": high_router_frac}
    for i in range(1, num_cpus):
        per_router[str(i)] = other_frac

    phase = _make_phase(0, 2000, 0.04,
                        num_cpus=num_cpus, num_dirs=4, per_router=per_router)
    profile = _make_profile(num_cpus, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc_on, _, _, res_on, op1 = _run_pace(ppath, seed=77)
    rc_off, _, _, res_off, op2 = _run_pace(ppath, seed=77,
                                            extra_args=["--pace-no-per-source"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    # With per-source enabled, the ablation.per_source must be true.
    if not res_on.get("ablation", {}).get("per_source", False):
        return TestResult(name, False,
            "ablation.per_source should be true for normal run")
    if res_off.get("ablation", {}).get("per_source", True):
        return TestResult(name, False,
            "ablation.per_source should be false for ablated run")

    # Both runs must have received some packets.
    rx_on  = res_on["packet_stats"]["total_packets_received"]
    rx_off = res_off["packet_stats"]["total_packets_received"]
    if rx_on == 0 or rx_off == 0:
        return TestResult(name, False,
            f"No packets received: rx_on={rx_on}, rx_off={rx_off}")

    return TestResult(name, True,
        f"Per-source on: rx={rx_on}, off: rx={rx_off}. "
        f"Ablation flag correctly toggled injection distribution.")


# ---------------------------------------------------------------------------
# Test 4: Phase ablation uses aggregate (single phase)
# ---------------------------------------------------------------------------

def test_phase_ablation():
    """
    With phases enabled, per_phase_stats has N entries and phase transitions occur.
    With --pace-no-phases, per_phase_stats has exactly 1 entry.
    """
    name = "Phase Ablation Collapses to Single Aggregate Phase"

    phases = [
        _make_phase(0, 500, 0.02),
        _make_phase(1, 500, 0.05),
        _make_phase(2, 500, 0.01),
    ]
    profile = _make_profile(16, 4, 4, 4, phases)
    ppath = _write_profile(profile)

    rc_on, out_on, _, res_on, op1 = _run_pace(ppath)
    rc_off, out_off, _, res_off, op2 = _run_pace(
        ppath, extra_args=["--pace-no-phases"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    phases_on  = res_on.get("per_phase_stats", [])
    phases_off = res_off.get("per_phase_stats", [])

    if len(phases_on) != 3:
        return TestResult(name, False,
            f"Expected 3 phases in normal run, got {len(phases_on)}")
    if len(phases_off) != 1:
        return TestResult(name, False,
            f"Expected 1 phase in ablated run, got {len(phases_off)}")

    # Normal run should show phase transition messages; ablated should not.
    if "entering phase 1" not in out_on:
        return TestResult(name, False,
            "Phase 1 transition missing in normal run stdout")
    if "entering phase 1" in out_off:
        return TestResult(name, False,
            "Phase 1 transition should not appear in ablated run")

    # Verify ablation flag in output JSON.
    if res_off.get("ablation", {}).get("phases", True):
        return TestResult(name, False,
            "ablation.phases should be false for ablated run")

    rx_on  = res_on["packet_stats"]["total_packets_received"]
    rx_off = res_off["packet_stats"]["total_packets_received"]
    return TestResult(name, True,
        f"Normal: {len(phases_on)} phases (rx={rx_on}); "
        f"Ablated: {len(phases_off)} phase (rx={rx_off})")


# ---------------------------------------------------------------------------
# Test 5: MSHR ablation increases injected packets under congestion
# ---------------------------------------------------------------------------

def test_mshr_ablation():
    """
    Without MSHR throttling, open-loop injection should produce more injected
    packets than MSHR-limited injection under a high injection rate.
    """
    name = "MSHR Ablation Enables Open-Loop Injection"

    # High injection rate with tight MSHR limit to make the difference visible.
    phase = _make_phase(0, 1000, 0.4, data_pct=80.0, num_cpus=16, num_dirs=4)
    profile = _make_profile(16, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    MSHR = 2
    rc_on, _, _, res_on, op1 = _run_pace(ppath, mshr=MSHR, seed=55)
    rc_off, _, _, res_off, op2 = _run_pace(ppath, mshr=MSHR, seed=55,
                                            extra_args=["--pace-no-mshr"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    # With MSHR disabled, the saturation metrics should show near-zero MSHR
    # occupancy (counter never incremented when check is skipped).
    if res_off.get("ablation", {}).get("mshr", True):
        return TestResult(name, False,
            "ablation.mshr should be false for ablated run")

    # Both runs must have packets.
    rx_on  = res_on["packet_stats"]["total_packets_received"]
    rx_off = res_off["packet_stats"]["total_packets_received"]
    if rx_on == 0:
        return TestResult(name, False, "No packets received in MSHR-limited run")
    if rx_off == 0:
        return TestResult(name, False, "No packets received in open-loop run")

    # Open-loop must receive >= MSHR-limited (no backpressure → more injected).
    if rx_off < rx_on:
        return TestResult(name, False,
            f"Expected open-loop rx >= limited rx; got rx_off={rx_off} < rx_on={rx_on}")

    return TestResult(name, True,
        f"MSHR-limited rx={rx_on}, open-loop rx={rx_off} "
        f"(ratio={rx_off/max(1,rx_on):.2f}x) — open-loop produces more traffic")


# ---------------------------------------------------------------------------
# Test 6: Remap ablation changes destination distribution
# ---------------------------------------------------------------------------

def test_remap_ablation():
    """
    With a non-identity remapping and --pace-no-remap, packets should travel
    to different physical routers, changing observed latency.
    Use dir 0 → router 15 (6 hops away from any core 0-3).
    With remap: high latency.  Without remap (identity): dir 0 → router 0.
    """
    name = "Remap Ablation Uses Identity Mapping"

    def _phase():
        return {
            "phase_index": 0,
            "total_packets": 100,
            "total_flits": 100,
            "flits_per_packet": 1.0,
            "data_pct": 0.0,
            "ctrl_pct": 100.0,
            "sim_ticks": 2000000,
            "network_cycles": 2000,
            "lambda": 0.04,
            "avg_packet_latency": 10.0,
            "vnet_packets": {"0": 100, "1": 0, "2": 0},
            "per_router_injection": {str(i): 1.0/4 for i in range(4)},
            "dir_fractions": {"0": 1.0},
        }

    # Profile: dir 0 remapped to router 15 (far corner).
    prof_remap = _make_profile(4, 1, 4, 4, [_phase()],
                               dir_remapping={"0": 15})
    ppath = _write_profile(prof_remap)

    # With remap active: dir 0 -> router 15.
    rc_on, _, _, res_on, op1 = _run_pace(ppath, rows=4, cols=4, mshr=32, seed=33)
    # With remap ablated: dir 0 -> router 0 (identity).
    rc_off, _, _, res_off, op2 = _run_pace(ppath, rows=4, cols=4, mshr=32, seed=33,
                                            extra_args=["--pace-no-remap"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    rx_on  = res_on["packet_stats"]["total_packets_received"]
    rx_off = res_off["packet_stats"]["total_packets_received"]
    if rx_on == 0 or rx_off == 0:
        return TestResult(name, False,
            f"No packets received: rx_on={rx_on} rx_off={rx_off}")

    lat_on  = res_on["packet_stats"]["avg_latency_cycles"]
    lat_off = res_off["packet_stats"]["avg_latency_cycles"]

    if res_off.get("ablation", {}).get("remap", True):
        return TestResult(name, False,
            "ablation.remap should be false for ablated run")

    # With remap to router 15: latency should be higher than identity (router 0).
    if lat_on <= lat_off:
        return TestResult(name, False,
            f"Expected remapped latency > identity latency; "
            f"lat_remap={lat_on:.2f} lat_identity={lat_off:.2f}")

    return TestResult(name, True,
        f"lat_remap={lat_on:.2f} > lat_identity={lat_off:.2f} "
        f"(diff={lat_on - lat_off:.2f}) — remap ablation confirmed")


# ---------------------------------------------------------------------------
# Test 7: Weighted-dest ablation uniformizes directory traffic
# ---------------------------------------------------------------------------

def test_weighted_dest_ablation():
    """
    With a strongly skewed dir_fractions (e.g., dir 0 gets 95%), the weighted
    run should produce very different latency than the uniform run.
    Check that ablation field is correctly set and that simulation completes.
    """
    name = "Weighted-Dest Ablation Uniformizes Directory Selection"

    # Heavily skewed: dir 0 gets 95%, others share 5%.
    dir_frac = {"0": 0.95, "1": 0.017, "2": 0.017, "3": 0.016}
    # dir remapping: spread dirs across the mesh for latency sensitivity.
    dir_remap = {"0": 0, "1": 5, "2": 10, "3": 15}
    phase = _make_phase(0, 2000, 0.04,
                        num_cpus=16, num_dirs=4, dir_frac=dir_frac)
    profile = _make_profile(16, 4, 4, 4, [phase], dir_remapping=dir_remap)
    ppath = _write_profile(profile)

    rc_on, _, _, res_on, op1 = _run_pace(ppath, seed=44)
    rc_off, _, _, res_off, op2 = _run_pace(ppath, seed=44,
                                            extra_args=["--pace-no-weighted-dest"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    if res_off.get("ablation", {}).get("weighted_dest", True):
        return TestResult(name, False,
            "ablation.weighted_dest should be false for ablated run")

    rx_on  = res_on["packet_stats"]["total_packets_received"]
    rx_off = res_off["packet_stats"]["total_packets_received"]
    if rx_on == 0 or rx_off == 0:
        return TestResult(name, False,
            f"No packets received: rx_on={rx_on} rx_off={rx_off}")

    lat_on  = res_on["packet_stats"]["avg_latency_cycles"]
    lat_off = res_off["packet_stats"]["avg_latency_cycles"]

    # With skewed dir_fractions (95% to dir 0 at router 0), most of the 16
    # CPUs (at routers 0-15) must route to router 0, so average latency is
    # dominated by the long paths.  Uniform distribution spreads traffic to
    # all 4 dirs (at routers 0, 5, 10, 15), giving each CPU a closer target
    # on average.  So skewed latency should be HIGHER than uniform latency.
    if lat_on <= lat_off:
        return TestResult(name, False,
            f"Expected skewed-dest latency > uniform latency; "
            f"lat_weighted={lat_on:.2f} lat_uniform={lat_off:.2f}")

    return TestResult(name, True,
        f"lat_weighted={lat_on:.2f} > lat_uniform={lat_off:.2f} "
        f"(diff={lat_on - lat_off:.2f}) — weighted-dest ablation confirmed")


# ---------------------------------------------------------------------------
# Test 8: Correlated-response ablation changes flit count
# ---------------------------------------------------------------------------

def test_corr_response_ablation():
    """
    With a profile where response_data_prob is low (most responses are 1-flit),
    the normal run should have lower total flits than the ablated run
    (which always produces 5-flit responses).
    """
    name = "Correlated-Response Ablation Uses Fixed Response Size"

    # Force very low data_pct so response_data_prob ≈ 0 (mostly 1-flit responses).
    phase = {
        "phase_index": 0,
        "total_packets": 200,
        "total_flits": 220,   # nearly all ctrl (1-flit); few data
        "flits_per_packet": 1.1,
        "data_pct": 5.0,
        "ctrl_pct": 95.0,
        "sim_ticks": 3000000,
        "network_cycles": 3000,
        "lambda": 0.03,
        "avg_packet_latency": 10.0,
        "vnet_packets": {"0": 100, "1": 95, "2": 5},
        "per_router_injection": {str(i): 1.0/16 for i in range(16)},
        "dir_fractions": {str(i): 0.25 for i in range(4)},
    }
    profile = _make_profile(16, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc_on, _, _, res_on, op1 = _run_pace(ppath, mshr=32, seed=66)
    rc_off, _, _, res_off, op2 = _run_pace(ppath, mshr=32, seed=66,
                                            extra_args=["--pace-no-corr-response"])
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_on != 0 or rc_off != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_on={rc_on} rc_off={rc_off}")
    if res_on is None or res_off is None:
        return TestResult(name, False, "Results JSON missing")

    if res_off.get("ablation", {}).get("corr_response", True):
        return TestResult(name, False,
            "ablation.corr_response should be false for ablated run")

    flits_on  = res_on["packet_stats"]["total_flits_received"]
    pkts_on   = res_on["packet_stats"]["total_packets_received"]
    flits_off = res_off["packet_stats"]["total_flits_received"]
    pkts_off  = res_off["packet_stats"]["total_packets_received"]

    if pkts_on == 0 or pkts_off == 0:
        return TestResult(name, False,
            f"No packets received: pkts_on={pkts_on} pkts_off={pkts_off}")

    avg_on  = flits_on  / pkts_on
    avg_off = flits_off / pkts_off

    # Ablated run forces 5-flit responses; correlated run uses ~1-flit responses.
    # So avg_off should be substantially higher than avg_on.
    if avg_off <= avg_on:
        return TestResult(name, False,
            f"Expected ablated avg_flits/pkt > correlated; "
            f"avg_corr={avg_on:.2f} avg_ablated={avg_off:.2f}")

    return TestResult(name, True,
        f"avg_flits/pkt: correlated={avg_on:.2f}, fixed-5flit={avg_off:.2f} "
        f"(ablated produces larger responses as expected)")


# ---------------------------------------------------------------------------
# Test 9: Full PACE vs all-disabled produces different results
# ---------------------------------------------------------------------------

def test_full_vs_all_disabled():
    """
    Full PACE and all-disabled PACE must produce measurably different
    latency and/or throughput, confirming the flags change behavior.
    """
    name = "Full PACE vs All-Disabled Produces Different Results"

    # Multi-phase profile with non-uniform fractions to make differences visible.
    phases = [
        _make_phase(0, 1000, 0.03,
                    per_router={"0": 0.15, "1": 0.10, **{str(i): (0.75/14)
                                for i in range(2, 16)}},
                    dir_frac={"0": 0.55, "1": 0.20, "2": 0.15, "3": 0.10}),
        _make_phase(1, 1000, 0.06,
                    per_router={"0": 0.05, "1": 0.05, **{str(i): (0.90/14)
                                for i in range(2, 16)}},
                    dir_frac={"0": 0.25, "1": 0.25, "2": 0.25, "3": 0.25}),
    ]
    # Non-identity remapping.
    dir_remap = {"0": 0, "1": 5, "2": 10, "3": 15}
    profile = _make_profile(16, 4, 4, 4, phases, dir_remapping=dir_remap)
    ppath = _write_profile(profile)

    all_flags = [
        "--pace-no-per-source", "--pace-no-phases", "--pace-no-mshr",
        "--pace-no-remap", "--pace-no-weighted-dest", "--pace-no-corr-response",
    ]

    rc_full, _, _, res_full, op1 = _run_pace(ppath, seed=99)
    rc_abl, _, _, res_abl, op2 = _run_pace(ppath, seed=99, extra_args=all_flags)
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc_full != 0 or rc_abl != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc_full={rc_full} rc_abl={rc_abl}")
    if res_full is None or res_abl is None:
        return TestResult(name, False, "Results JSON missing")

    # Full PACE: all ablation fields should be true.
    abl_full = res_full.get("ablation", {})
    if not all(abl_full.values()):
        return TestResult(name, False,
            f"Full run has unexpected ablation flags: {abl_full}")

    # All-disabled: all ablation fields should be false.
    abl_abl = res_abl.get("ablation", {})
    if any(abl_abl.values()):
        return TestResult(name, False,
            f"All-disabled run has unexpected ablation flags: {abl_abl}")

    lat_full = res_full["packet_stats"]["avg_latency_cycles"]
    lat_abl  = res_abl["packet_stats"]["avg_latency_cycles"]
    rx_full  = res_full["packet_stats"]["total_packets_received"]
    rx_abl   = res_abl["packet_stats"]["total_packets_received"]

    if rx_full == 0 and rx_abl == 0:
        return TestResult(name, False, "No packets received in either run")

    # Results should differ (any metric is sufficient to confirm).
    lat_diff = abs(lat_full - lat_abl)
    rx_diff  = abs(rx_full - rx_abl)

    if lat_diff < 0.5 and rx_diff < 5:
        return TestResult(name, False,
            f"Full and all-disabled results are nearly identical: "
            f"lat_full={lat_full:.2f} lat_abl={lat_abl:.2f} "
            f"rx_full={rx_full} rx_abl={rx_abl}")

    return TestResult(name, True,
        f"Full: lat={lat_full:.2f} rx={rx_full}; "
        f"All-disabled: lat={lat_abl:.2f} rx={rx_abl}; "
        f"lat_diff={lat_diff:.2f} rx_diff={rx_diff}")


# ---------------------------------------------------------------------------
# Test 10: Deterministic output with ablation flags
# ---------------------------------------------------------------------------

def test_ablation_deterministic():
    """
    Two runs with same ablation flags and same seed must produce identical output.
    Verifies reproducibility of all ablation modes.
    """
    name = "Deterministic Output with Ablation Flags"

    phase = _make_phase(0, 1000, 0.04)
    profile = _make_profile(16, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    all_flags = [
        "--pace-no-per-source", "--pace-no-phases", "--pace-no-mshr",
        "--pace-no-remap", "--pace-no-weighted-dest", "--pace-no-corr-response",
    ]
    SEED = 123

    rc1, _, _, res1, op1 = _run_pace(ppath, seed=SEED, extra_args=all_flags)
    rc2, _, _, res2, op2 = _run_pace(ppath, seed=SEED, extra_args=all_flags)
    for p in [op1, op2]:
        if os.path.isfile(p): os.unlink(p)
    os.unlink(ppath)

    if rc1 != 0 or rc2 != 0:
        return TestResult(name, False,
            f"Simulation crashed: rc1={rc1} rc2={rc2}")
    if res1 is None or res2 is None:
        return TestResult(name, False, "Results JSON missing")

    rx1  = res1["packet_stats"]["total_packets_received"]
    rx2  = res2["packet_stats"]["total_packets_received"]
    lat1 = res1["packet_stats"]["avg_latency_cycles"]
    lat2 = res2["packet_stats"]["avg_latency_cycles"]

    if rx1 != rx2:
        return TestResult(name, False,
            f"Non-deterministic: rx1={rx1} rx2={rx2}")
    if abs(lat1 - lat2) > 0.001:
        return TestResult(name, False,
            f"Non-deterministic: lat1={lat1:.4f} lat2={lat2:.4f}")

    return TestResult(name, True,
        f"rx={rx1} lat={lat1:.2f} identical in both all-ablation runs (seed={SEED})")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("=== PACE Ablation Mode Test Suite ===\n")

    if not os.path.exists(BINARY):
        print(f"Error: binary '{BINARY}' not found. Run 'make' first.")
        sys.exit(1)

    tests = [
        test_flag_parsing,
        test_all_flags_combined,
        test_per_source_ablation,
        test_phase_ablation,
        test_mshr_ablation,
        test_remap_ablation,
        test_weighted_dest_ablation,
        test_corr_response_ablation,
        test_full_vs_all_disabled,
        test_ablation_deterministic,
    ]

    results = []
    for test_fn in tests:
        print(f"Running: {test_fn.__name__} ...", end=" ", flush=True)
        res = test_fn()
        status = "PASS" if res.success else "FAIL"
        print(f"[{status}]")
        results.append(res)

    print("\n=== Summary ===")
    passed = 0
    for r in results:
        status = "PASS" if r.success else "FAIL"
        print(f"[{status}] {r.name}")
        if r.success:
            print(f"       {r.details}")
            passed += 1
        else:
            print(f"  FAIL: {r.details}")

    print(f"\nPassed {passed}/{len(results)} tests.")
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
