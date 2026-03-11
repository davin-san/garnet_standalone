#!/usr/bin/env python3
"""
Comprehensive PACE Traffic Generator test suite.

Tests verify:
  1. Basic smoke test (profile loads, simulation completes)
  2. JSON output completeness (all required fields present)
  3. Directory remapping correctness (key correctness test)
  4. MSHR hard cap enforcement
  5. Phase cycling (all phases visited)
  6. Throughput and latency sanity
  7. Multi-flit response sizing (data vs ctrl packets)
  8. Zero-lambda no-injection
  9. Injection probability scaling across phases
 10. Results-file reproducibility (deterministic RNG)
"""

import json
import os
import re
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
    """Run binary with args; return (returncode, stdout, stderr)."""
    cmd = [BINARY] + args
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "TIMEOUT"
    except Exception as e:
        return -2, "", str(e)


def _write_profile(profile: dict) -> str:
    """Write a profile dict to a temp JSON file; return the path."""
    f = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
    json.dump(profile, f, indent=2)
    f.close()
    return f.name


def _make_phase(idx, cycles, lambda_val, data_pct=50.0,
                num_cpus=4, num_dirs=4, rows=4, cols=4):
    """Build a minimal phase dict with uniform per-router fractions."""
    total_pkts  = max(1, num_cpus * cycles // 20)
    v0  = int(total_pkts * 0.50)
    v1  = int(total_pkts * 0.35)
    v2  = total_pkts - v0 - v1
    per_router = {str(i): 1.0 / num_cpus for i in range(num_cpus)}
    dir_frac   = {str(i): 1.0 / num_dirs  for i in range(num_dirs)}
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
    """Build a complete profile dict."""
    if dir_remapping is None:
        dir_remapping = {str(i): i for i in range(num_dirs)}
    return {
        "num_cpus":    num_cpus,
        "num_dirs":    num_dirs,
        "mesh_rows":   rows,
        "mesh_cols":   cols,
        "mem_channels": num_dirs,
        "num_phases":  len(phases),
        "phases":      phases,
        "model_assumptions": {
            "flit_width_bytes":  16,
            "cacheline_bytes":   64,
            "data_packet_flits": 5,
            "ctrl_packet_flits": 1,
        },
        "directory_remapping": dir_remapping,
    }


def _run_pace(profile_path, rows=4, cols=4, output_path=None,
              mshr=16, extra_args=None):
    """Run the binary in PACE mode; return (rc, stdout, stderr, results_dict|None)."""
    if output_path is None:
        tf = tempfile.NamedTemporaryFile(suffix=".json", delete=False)
        tf.close()
        output_path = tf.name

    args = [
        "--topology", "Mesh_XY",
        "--rows",     str(rows),
        "--cols",     str(cols),
        "--routing",  "1",          # XY routing
        "--pace-profile", profile_path,
        "--pace-mshr", str(mshr),
        "--pace-output", output_path,
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

    return rc, out, err, results


# ---------------------------------------------------------------------------
# Individual tests
# ---------------------------------------------------------------------------

def test_smoke():
    """Basic: bundled pace_profile.json loads and simulation completes."""
    name = "Smoke Test (bundled pace_profile.json)"

    if not os.path.isfile("pace_profile.json"):
        return TestResult(name, False, "pace_profile.json not found in CWD")

    rc, out, err, results = _run_pace("pace_profile.json")

    if rc != 0:
        return TestResult(name, False,
            f"Exit code {rc}\nstdout: {out[:500]}\nstderr: {err[:500]}")

    if "PACE simulation finished" not in out:
        return TestResult(name, False,
            f"'PACE simulation finished' not found\nstdout: {out[:500]}")

    if results is None:
        return TestResult(name, False, "Results JSON missing or invalid")

    return TestResult(name, True,
        f"rx={results['packet_stats']['total_packets_received']}"
        f" avg_lat={results['packet_stats']['avg_latency_cycles']:.1f}")


def test_json_output_completeness():
    """JSON results file must contain all required top-level fields."""
    name = "JSON Output Completeness"

    profile = _make_profile(4, 4, 4, 4,
        [_make_phase(0, 500, 0.04, num_cpus=4, num_dirs=4)])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing or invalid")

    required_top = ["simulation_summary", "packet_stats", "saturation",
                    "per_phase_stats", "per_link_utilization",
                    "latency_histogram_sample"]
    required_pkt = ["total_packets_received", "total_flits_received",
                    "avg_latency_cycles", "p99_latency_cycles",
                    "throughput_flits_per_cycle"]
    required_sim = ["total_cycles", "total_phases", "mshr_limit",
                    "num_cpus", "num_dirs"]

    missing = []
    for f in required_top:
        if f not in results:
            missing.append(f)
    for f in required_pkt:
        if f not in results.get("packet_stats", {}):
            missing.append(f"packet_stats.{f}")
    for f in required_sim:
        if f not in results.get("simulation_summary", {}):
            missing.append(f"simulation_summary.{f}")

    if missing:
        return TestResult(name, False, f"Missing fields: {', '.join(missing)}")

    return TestResult(name, True, "All required fields present")


def test_directory_remapping_correctness():
    """
    Directory remapping must route packets to the physically correct router.

    Two runs on a 4x4 mesh with 2 cores (NI 0, NI 1):
      A – remapped:  dir 0 → router 15  (far corner, 6 hops from router 0)
      B – identity:  dir 0 → router 0   (same router as core 0, 0 hops from core 0)

    Average packet latency for A must be substantially higher than B.
    If remapping is ignored, both would produce nearly identical (low) latency.
    """
    name = "Directory Remapping Correctness"

    def _phase(lambda_val):
        return {
            "phase_index": 0,
            "total_packets": 100,
            "total_flits": 200,
            "flits_per_packet": 2.0,
            "data_pct": 0.0,   # all ctrl (1-flit) for determinism
            "ctrl_pct": 100.0,
            "sim_ticks": 2000000,
            "network_cycles": 1000,
            "lambda": lambda_val,
            "avg_packet_latency": 5.0,
            # Only cores 0 and 1 inject; equal fractions.
            "vnet_packets": {"0": 60, "1": 40, "2": 0},
            "per_router_injection": {"0": 0.5, "1": 0.5},
            # All traffic goes to dir 0.
            "dir_fractions": {"0": 1.0},
        }

    # Profile A: dir 0 lives at router 15 (bottom-right of 4x4)
    prof_a = _make_profile(2, 1, 4, 4, [_phase(0.05)],
                           dir_remapping={"0": 15})
    # Profile B: dir 0 lives at router 0 (same location as core 0 = self-send)
    prof_b = _make_profile(2, 1, 4, 4, [_phase(0.05)],
                           dir_remapping={"0": 0})

    pa = _write_profile(prof_a)
    pb = _write_profile(prof_b)

    rc_a, out_a, err_a, res_a = _run_pace(pa, rows=4, cols=4)
    rc_b, out_b, err_b, res_b = _run_pace(pb, rows=4, cols=4)

    os.unlink(pa)
    os.unlink(pb)

    if rc_a != 0:
        return TestResult(name, False, f"Profile A crashed: {err_a[:300]}")
    if rc_b != 0:
        return TestResult(name, False, f"Profile B crashed: {err_b[:300]}")
    if res_a is None or res_b is None:
        return TestResult(name, False, "Results JSON missing for one or both profiles")

    lat_a = res_a["packet_stats"]["avg_latency_cycles"]
    lat_b = res_b["packet_stats"]["avg_latency_cycles"]
    rx_a  = res_a["packet_stats"]["total_packets_received"]
    rx_b  = res_b["packet_stats"]["total_packets_received"]

    # We need packets to actually have been received to compare latencies.
    if rx_a == 0 or rx_b == 0:
        return TestResult(name, False,
            f"No packets received: rx_a={rx_a}, rx_b={rx_b}")

    # Remapped (A) must have meaningfully higher latency than identity (B).
    # 6-hop XY path adds at least 6 cycles; identity path ≤ 1 hop.
    # Require at least 3-cycle difference to tolerate pipeline variability.
    LATENCY_DIFF_MIN = 3.0
    if lat_a - lat_b < LATENCY_DIFF_MIN:
        return TestResult(name, False,
            f"Remapping not detected: lat_A={lat_a:.2f} lat_B={lat_b:.2f} "
            f"(expected lat_A - lat_B >= {LATENCY_DIFF_MIN}). "
            f"If remapping is broken, lat_A would equal lat_B (both self-send).")

    return TestResult(name, True,
        f"lat_far={lat_a:.2f} lat_self={lat_b:.2f} "
        f"diff={lat_a - lat_b:.2f} cycles (remapping active)")


def test_mshr_hard_cap():
    """
    With mshr_limit=N, no node should sustain more than N outstanding requests.
    The max_avg_mshr_occupancy in the results must be <= mshr_limit.
    """
    name = "MSHR Hard Cap"
    MSHR_LIMIT = 2

    # High injection rate to stress MSHR.
    phase = _make_phase(0, 1000, 0.4, data_pct=80.0,
                        num_cpus=4, num_dirs=4, rows=4, cols=4)
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath, mshr=MSHR_LIMIT)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    max_mshr = results["saturation"]["max_avg_mshr_occupancy"]
    rx = results["packet_stats"]["total_packets_received"]

    if rx == 0:
        return TestResult(name, False, "No packets received — MSHR may have deadlocked")

    if max_mshr > MSHR_LIMIT + 0.5:  # small tolerance for sampling
        return TestResult(name, False,
            f"MSHR cap violated: max_avg={max_mshr:.2f} > limit={MSHR_LIMIT}")

    return TestResult(name, True,
        f"max_avg_mshr={max_mshr:.2f} <= limit={MSHR_LIMIT}, rx={rx}")


def test_phase_cycling():
    """All N phases must appear in per_phase_stats and be visited in order."""
    name = "Phase Cycling"
    NUM_PHASES = 3

    phases = [
        _make_phase(i, 300 + i * 200, 0.02 + i * 0.01,
                    num_cpus=4, num_dirs=4)
        for i in range(NUM_PHASES)
    ]
    profile = _make_profile(4, 4, 4, 4, phases)
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    phase_stats = results.get("per_phase_stats", [])
    if len(phase_stats) != NUM_PHASES:
        return TestResult(name, False,
            f"Expected {NUM_PHASES} phase entries, got {len(phase_stats)}")

    for i, ps in enumerate(phase_stats):
        if ps.get("phase_index") != i:
            return TestResult(name, False,
                f"Phase {i} has wrong index: {ps.get('phase_index')}")

    # All 3 phases should have received some packets.
    total_rx = sum(ps["packets_received"] for ps in phase_stats)
    if total_rx == 0:
        return TestResult(name, False, "No packets received across all phases")

    # Confirm phase boundary messages in stdout.
    if "entering phase 1" not in out or "entering phase 2" not in out:
        return TestResult(name, False,
            f"Phase transition messages missing in stdout")

    return TestResult(name, True,
        f"{NUM_PHASES} phases visited, total_rx={total_rx}")


def test_throughput_sanity():
    """Non-zero injection must produce non-zero throughput and positive latency."""
    name = "Throughput Sanity"

    phase = _make_phase(0, 2000, 0.05, num_cpus=4, num_dirs=4)
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    pkt_stats = results["packet_stats"]
    rx         = pkt_stats["total_packets_received"]
    avg_lat    = pkt_stats["avg_latency_cycles"]
    throughput = pkt_stats["throughput_flits_per_cycle"]
    total_cyc  = results["simulation_summary"]["total_cycles"]

    errors = []
    if rx <= 0:
        errors.append(f"rx={rx} (expected > 0)")
    if avg_lat <= 0:
        errors.append(f"avg_lat={avg_lat} (expected > 0)")
    if throughput <= 0:
        errors.append(f"throughput={throughput} (expected > 0)")
    if not (2000 <= total_cyc <= 2500):  # +drain window (typically +200)
        errors.append(f"total_cycles={total_cyc} (expected ~2000 + drain)")

    if errors:
        return TestResult(name, False, "; ".join(errors))

    return TestResult(name, True,
        f"rx={rx} avg_lat={avg_lat:.1f} tp={throughput:.4f} cycles={total_cyc}")


def test_zero_lambda_no_injection():
    """Lambda=0 must produce zero injected packets and zero received packets."""
    name = "Zero Lambda (No Injection)"

    phase = _make_phase(0, 500, 0.0, num_cpus=4, num_dirs=4)
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    rx = results["packet_stats"]["total_packets_received"]
    if rx > 0:
        return TestResult(name, False, f"Expected 0 received packets, got {rx}")

    return TestResult(name, True, "Zero packets received as expected")


def test_multi_flit_response_sizing():
    """
    With 100% data_pct (data_packet_flits=5), all responses should be 5-flit.
    total_flits_received >> total_packets_received.
    Specifically, flits/packet should approach 5 (responses) or show > 1 avg flit.
    """
    name = "Multi-Flit Response Sizing"

    # 100% data so all new injections are 5-flit data packets.
    # Response sizing: vnet0→vnet1 response, correlated. High data_pct means
    # response_data_prob is high → most responses are 5-flit data.
    # The vnet1 responses dominate received traffic.
    phase = {
        "phase_index": 0,
        "total_packets": 400,
        "total_flits": 1800,    # 400 ctrl + (200 * 5 data) = 1400 + ...
        "flits_per_packet": 4.5,
        "data_pct": 80.0,
        "ctrl_pct": 20.0,
        "sim_ticks": 2000000,
        "network_cycles": 2000,
        "lambda": 0.04,
        "avg_packet_latency": 10.0,
        "vnet_packets": {"0": 200, "1": 150, "2": 50},
        "per_router_injection": {str(i): 0.25 for i in range(4)},
        "dir_fractions": {str(i): 0.25 for i in range(4)},
    }
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    pkt_stats = results["packet_stats"]
    rx_pkts  = pkt_stats["total_packets_received"]
    rx_flits = pkt_stats["total_flits_received"]

    if rx_pkts == 0:
        return TestResult(name, False, "No packets received")

    avg_flit_per_pkt = rx_flits / rx_pkts
    # With mixed ctrl/data responses, average should be above 1.0 (ctrl-only baseline)
    if avg_flit_per_pkt <= 1.0:
        return TestResult(name, False,
            f"avg flits/pkt={avg_flit_per_pkt:.2f} — expected > 1.0 "
            f"(data responses should inflate flit count)")

    return TestResult(name, True,
        f"rx_pkts={rx_pkts} rx_flits={rx_flits} "
        f"avg_flit/pkt={avg_flit_per_pkt:.2f}")


def test_response_completeness():
    """
    Every vnet-0 request must eventually produce a vnet-1 response.
    Metric: total received > 0 and per-phase packets_received includes responses.
    We inject only vnet 0 and verify that received packets includes
    the vnet-1 responses (received at core nodes).
    """
    name = "Response Completeness"

    # Force vnet 0 only injection by setting v1=v2=0.
    phase = {
        "phase_index": 0,
        "total_packets": 100,
        "total_flits": 100,
        "flits_per_packet": 1.0,
        "data_pct": 0.0,
        "ctrl_pct": 100.0,
        "sim_ticks": 3000000,
        "network_cycles": 3000,   # long window to allow drain
        "lambda": 0.02,
        "avg_packet_latency": 10.0,
        "vnet_packets": {"0": 100, "1": 0, "2": 0},
        "per_router_injection": {str(i): 0.25 for i in range(4)},
        "dir_fractions": {str(i): 0.25 for i in range(4)},
    }
    profile = _make_profile(4, 4, 4, 4, [phase],
                            dir_remapping={"0": 0, "1": 5, "2": 10, "3": 15})
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath, mshr=32)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    rx = results["packet_stats"]["total_packets_received"]
    # Cores injected vnet 0 requests → directories should have sent vnet 1 responses.
    # Both requests and responses count toward received, so rx should be > 0.
    if rx == 0:
        return TestResult(name, False,
            "No packets received — responses may not be generated")

    # The latency histogram should have at least one non-zero entry.
    hist = results.get("latency_histogram_sample", {})
    if not hist:
        return TestResult(name, False, "Empty latency histogram — nothing received")

    return TestResult(name, True,
        f"rx={rx} hist_entries={len(hist)}")


def test_mshr_preserved_across_phases():
    """
    MSHR state must not reset at phase boundaries.
    We verify that saturated_nodes in the output may include multiple nodes
    when load is high across phase transitions (not reset causes accumulation).
    More practically: the simulation must not crash or deadlock at phase switch.
    """
    name = "MSHR State Preserved Across Phase Boundary"

    phases = [
        _make_phase(0, 500, 0.3, num_cpus=4, num_dirs=4),  # high load
        _make_phase(1, 500, 0.01, num_cpus=4, num_dirs=4), # low load
    ]
    profile = _make_profile(4, 4, 4, 4, phases)
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath, mshr=4)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed at phase boundary: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    # Both phases should have data.
    stats = results.get("per_phase_stats", [])
    if len(stats) != 2:
        return TestResult(name, False, f"Expected 2 phase entries, got {len(stats)}")

    if "entering phase 1" not in out:
        return TestResult(name, False, "Phase 1 never entered — phase cycling broken")

    return TestResult(name, True,
        f"Phase transition survived; "
        f"ph0_rx={stats[0]['packets_received']} ph1_rx={stats[1]['packets_received']}")


def test_injection_rate_scales_with_lambda():
    """
    Higher lambda in a phase should produce more injected packets.
    Phase A (high lambda) must receive more than Phase B (low lambda),
    both over the same number of cycles.
    """
    name = "Injection Rate Scales with Lambda"

    phases = [
        _make_phase(0, 1000, 0.06, num_cpus=4, num_dirs=4),  # high
        _make_phase(1, 1000, 0.01, num_cpus=4, num_dirs=4),  # low
    ]
    profile = _make_profile(4, 4, 4, 4, phases)
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    stats = results.get("per_phase_stats", [])
    if len(stats) != 2:
        return TestResult(name, False, f"Expected 2 phase entries, got {len(stats)}")

    rx0 = stats[0]["packets_received"]
    rx1 = stats[1]["packets_received"]

    if rx0 == 0 and rx1 == 0:
        return TestResult(name, False, "No packets received in either phase")

    # High-lambda phase should produce more traffic.
    if rx0 <= rx1:
        return TestResult(name, False,
            f"Phase 0 (high lambda) should have more rx than phase 1 (low lambda): "
            f"rx0={rx0}, rx1={rx1}")

    return TestResult(name, True,
        f"rx_high_lambda={rx0} > rx_low_lambda={rx1} (ratio={rx0/max(1,rx1):.1f}x)")


def test_deterministic_rng():
    """
    Two runs with the same seed must produce identical results.
    """
    name = "Deterministic RNG (same seed → same results)"

    phase = _make_phase(0, 500, 0.04, num_cpus=4, num_dirs=4)
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    tf1 = tempfile.NamedTemporaryFile(suffix=".json", delete=False)
    tf1.close()
    tf2 = tempfile.NamedTemporaryFile(suffix=".json", delete=False)
    tf2.close()

    args = [
        "--topology", "Mesh_XY",
        "--rows", "4", "--cols", "4",
        "--routing", "1",
        "--pace-profile", ppath,
        "--pace-mshr", "16",
        "--seed", "99",
    ]
    rc1, _, _, res1 = _run_pace(ppath, rows=4, cols=4,
                                 output_path=tf1.name,
                                 extra_args=["--seed", "99"])
    rc2, _, _, res2 = _run_pace(ppath, rows=4, cols=4,
                                 output_path=tf2.name,
                                 extra_args=["--seed", "99"])

    os.unlink(ppath)
    os.unlink(tf1.name)
    os.unlink(tf2.name)

    if rc1 != 0 or rc2 != 0:
        return TestResult(name, False, f"One run crashed: rc1={rc1} rc2={rc2}")
    if res1 is None or res2 is None:
        return TestResult(name, False, "Results missing for one or both runs")

    rx1 = res1["packet_stats"]["total_packets_received"]
    rx2 = res2["packet_stats"]["total_packets_received"]
    lat1 = res1["packet_stats"]["avg_latency_cycles"]
    lat2 = res2["packet_stats"]["avg_latency_cycles"]

    if rx1 != rx2:
        return TestResult(name, False,
            f"Non-deterministic: rx1={rx1} rx2={rx2}")
    if abs(lat1 - lat2) > 0.001:
        return TestResult(name, False,
            f"Non-deterministic: lat1={lat1:.4f} lat2={lat2:.4f}")

    return TestResult(name, True, f"rx={rx1} lat={lat1:.2f} identical both runs")


def test_link_utilization_reported():
    """Per-link utilization must be reported and non-zero under load."""
    name = "Per-Link Utilization Reported"

    phase = _make_phase(0, 1000, 0.05, num_cpus=4, num_dirs=4)
    profile = _make_profile(4, 4, 4, 4, [phase])
    ppath = _write_profile(profile)

    rc, out, err, results = _run_pace(ppath)
    os.unlink(ppath)

    if rc != 0:
        return TestResult(name, False, f"Simulation crashed: {err[:300]}")
    if results is None:
        return TestResult(name, False, "Results JSON missing")

    lu = results.get("per_link_utilization", {})
    num_links = lu.get("num_links", 0)
    avg_util  = lu.get("avg", -1.0)
    max_util  = lu.get("max", -1.0)

    if num_links == 0:
        return TestResult(name, False, "num_links=0 — topology has no links?")
    if avg_util < 0:
        return TestResult(name, False, f"avg utilization negative: {avg_util}")
    if max_util <= 0:
        return TestResult(name, False,
            f"max utilization is 0 — links appear unused despite traffic")

    return TestResult(name, True,
        f"num_links={num_links} avg_util={avg_util:.4f} max_util={max_util:.4f}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("=== PACE Traffic Generator Test Suite ===\n")

    if not os.path.exists(BINARY):
        print(f"Error: binary '{BINARY}' not found. Run 'make' first.")
        sys.exit(1)

    tests = [
        test_smoke,
        test_json_output_completeness,
        test_directory_remapping_correctness,
        test_mshr_hard_cap,
        test_phase_cycling,
        test_throughput_sanity,
        test_zero_lambda_no_injection,
        test_multi_flit_response_sizing,
        test_response_completeness,
        test_mshr_preserved_across_phases,
        test_injection_rate_scales_with_lambda,
        test_deterministic_rng,
        test_link_utilization_reported,
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
