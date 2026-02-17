import subprocess
import re
import itertools
import os
import csv
from concurrent.futures import ProcessPoolExecutor

# --- Configuration ---
BINARY = "./garnet_standalone"
OUTPUT_FILE = "exploration_results.csv"
MAX_WORKERS = os.cpu_count() # Run in parallel on all cores

# --- Sweep Space ---
topologies = ["Mesh_XY"]
dimensions = [(4, 4), (8, 8)] # (rows, cols)
injection_rates = [0.01, 0.1]
packet_sizes = [1, 4]
routing_algos = [0, 1] # 0: Table, 1: XY

def run_sim(params):
    topo, (rows, cols), rate, pkt_size, routing = params
    
    cmd = [
        BINARY,
        "--topology", topo,
        "--rows", str(rows),
        "--cols", str(cols),
        "--rate", str(rate),
        "--packet-size", str(pkt_size),
        "--routing", str(routing),
        "--cycles", "1000"
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        out = result.stdout
        
        # Extract metrics
        lat_match = re.search(r"Average Network Latency:\s+(\d+\.?\d*)", out)
        rx_match = re.search(r"Total Packets Received:\s+(\d+)", out)
        util_match = re.search(r"Average Link Utilization:\s+(\d+\.?\d*)", out)
        
        return {
            "Topology": os.path.basename(topo),
            "Rows": rows,
            "Cols": cols,
            "Rate": rate,
            "PktSize": pkt_size,
            "Routing": "XY" if routing == 1 else "Table",
            "Latency": float(lat_match.group(1)) if lat_match else "N/A",
            "Throughput": int(rx_match.group(1)) if rx_match else 0,
            "Utilization": float(util_match.group(1)) if util_match else 0.0
        }
    except Exception as e:
        return {"Topology": topo, "Error": str(e)}

def main():
    print(f"=== Garnet Interconnect Exploration ===")
    print(f"Parallel workers: {MAX_WORKERS}")
    
    # Generate all combinations
    sweep_space = list(itertools.product(topologies, dimensions, injection_rates, packet_sizes, routing_algos))
    print(f"Total simulations to run: {len(sweep_space)}")

    results = []
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        results = list(executor.map(run_sim, sweep_space))

    # Sort results for readability
    results = [r for r in results if "Error" not in r]
    if not results:
        print("No valid results collected.")
        return
    results.sort(key=lambda x: (x["Topology"], x["Rows"], x["Rate"]))

    # Save to CSV
    keys = results[0].keys()
    with open(OUTPUT_FILE, 'w', newline='') as f:
        dict_writer = csv.DictWriter(f, keys)
        dict_writer.writeheader()
        dict_writer.writerows(results)

    # Print Summary Table
    print(f"\n{'Topo':<15} | {'Size':<5} | {'Rate':<5} | {'Pkt':<3} | {'Latency':<10} | {'Util %':<5}")
    print("-" * 65)
    for r in results[:20]: # Show first 20 results
        print(f"{r['Topology']:<15} | {r['Rows']}x{r['Cols']:<2} | {r['Rate']:<5} | {r['PktSize']:<3} | {r['Latency']:<10.4f} | {r['Utilization']:<5.2f}")
    
    print(f"\nFull results saved to {OUTPUT_FILE}")

if __name__ == "__main__":
    if not os.path.exists(BINARY):
        print("Error: Binary not found. Run 'make' first.")
    else:
        main()