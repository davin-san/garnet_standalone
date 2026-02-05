import subprocess
import re
import statistics
import os

# Configuration
garnet_bin = "./garnet_standalone"
cycles = 10000
injection_rate = 0.05
num_runs = 10

benchmarks = [
    {
        "name": "Mesh_XY (8x8)",
        "topology": "../configs/topologies/Mesh_XY.py",
        "args": ["--rows", "8", "--cols", "8", "--routing", "1"]
    },
    {
        "name": "Crossbar (16 nodes)",
        "topology": "../configs/topologies/Crossbar.py",
        "args": ["--rows", "1", "--cols", "16", "--routing", "0"]
    },
    {
        "name": "Pt2Pt (16 nodes)",
        "topology": "../configs/topologies/Pt2Pt.py",
        "args": ["--rows", "1", "--cols", "16", "--routing", "0"]
    }
]

print(f"{'Topology':<20} | {'Run':<5} | {'Latency':<10}")
print("-" * 45)

results = {}

for bench in benchmarks:
    latencies = []
    print(f"Benchmarking {bench['name']}...")
    
    for i in range(1, num_runs + 1):
        cmd = [
            garnet_bin,
            "--topology", bench["topology"],
            "--cycles", str(cycles),
            "--rate", str(injection_rate),
            "--seed", str(i)
        ] + bench["args"]
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            output = result.stdout
            
            match = re.search(r"Average Network Latency: (\d+(\.\d+)?)", output)
            if match:
                lat = float(match.group(1))
                latencies.append(lat)
                print(f"{bench['name']:<20} | {i:<5} | {lat:<10.4f}")
            else:
                # Handle no packets received gracefully
                pass
                
        except subprocess.CalledProcessError as e:
            print(f"Error running {bench['name']} run {i}: {e}")

    if latencies:
        avg_lat = statistics.mean(latencies)
        results[bench['name']] = avg_lat
    else:
        results[bench['name']] = 0.0

print("\n" + "="*35)
print("FINAL RESULTS (10-Run Average)")
print("="*35)
print(f"{'Topology':<20} | {'Avg Latency':<15}")
print("-" * 35)
for name, avg in results.items():
    print(f"{name:<20} | {avg:<15.4f}")
print("="*35)