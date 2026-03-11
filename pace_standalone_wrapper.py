#!/usr/bin/env python3
import json
import argparse
import subprocess
import os
import sys

def main():
    parser = argparse.ArgumentParser(description="Weighted aggregation wrapper for PACE standalone sim")
    parser.add_argument("--pace-profile", required=True, help="Path to original pace_profile.json")
    parser.add_argument("--pace-packets-per-node", type=int, default=500)
    parser.add_argument("--pace-temporal-floor", type=float, default=2.0)
    parser.add_argument("--topology", default="Mesh_XY")
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--cols", type=int, default=4)
    parser.add_argument("--output", default="final_result.json")
    
    # Forward other unknown args to garnet_standalone
    args, unknown = parser.parse_known_args()

    # 1. Load original profile to get ground truth packet counts
    with open(args.pace_profile, 'r') as f:
        profile = json.load(f)
    
    original_phase_packets = [ph['total_packets'] for ph in profile['phases']]
    total_original_packets = sum(original_phase_packets)

    # 2. Run the standalone simulator
    standalone_bin = "./garnet_standalone"
    if not os.path.exists(standalone_bin):
        print(f"Error: {standalone_bin} not found. Run make first.")
        sys.exit(1)

    cmd = [
        standalone_bin,
        "--pace-profile", args.pace_profile,
        "--pace-packets-per-node", str(args.pace_packets_per_node),
        "--pace-temporal-floor", str(args.pace_temporal_floor),
        "--topology", args.topology,
        "--rows", str(args.rows),
        "--cols", str(args.cols),
        "--pace-output", "standalone_temp.json"
    ] + unknown

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("Simulator failed.")
        sys.exit(result.returncode)

    # 3. Load standalone results
    with open("standalone_temp.json", 'r') as f:
        results = json.load(f)
    
    standalone_phases = results['per_phase_stats']
    
    # 4. Weighted Aggregation
    # L_final = sum(L_phase_standalone * P_phase_original) / sum(P_phase_original)
    
    weighted_latency_sum = 0.0
    
    for i, ph_res in enumerate(standalone_phases):
        l_standalone = ph_res['avg_latency_cycles']
        p_original = original_phase_packets[i]
        weighted_latency_sum += l_standalone * p_original
    
    final_avg_latency = weighted_latency_sum / total_original_packets if total_original_packets > 0 else 0
    
    # 5. Output final JSON
    final_out = {
        "final_avg_latency": final_avg_latency,
        "original_total_packets": total_original_packets,
        "standalone_execution_cycles": results['simulation_summary']['total_cycles'],
        "phases": []
    }
    
    for i, ph_res in enumerate(standalone_phases):
        final_out["phases"].append({
            "phase_index": i,
            "standalone_avg_latency": ph_res['avg_latency_cycles'],
            "original_packet_count": original_phase_packets[i],
            "standalone_packet_count": ph_res['packets_received']
        })
    
    with open(args.output, 'w') as f:
        json.dump(final_out, f, indent=2)
    
    print(f"\nFinal Aggregated Latency: {final_avg_latency:.4f} cycles")
    print(f"Results saved to {args.output}")

if __name__ == "__main__":
    main()
