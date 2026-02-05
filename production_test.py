import subprocess
import re
import sys
import os

# Configuration
BINARY = "./garnet_standalone"
TIMEOUT = 60 # Seconds per test

class TestResult:
    def __init__(self, name, success, details=""):
        self.name = name
        self.success = success
        self.details = details

def run_test(name, args, expected_min_packets=0, max_latency=None):
    cmd = [BINARY] + args
    print(f"Running Test: {name}...")
    print(f"  Command: {' '.join(cmd)}")
    
    try:
        # Run with timeout to catch deadlocks
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT)
        
        if result.returncode != 0:
            return TestResult(name, False, f"Crash/Exit Code {result.returncode}\nStderr: {result.stderr}")

        output = result.stdout
        
        # Parse Metrics
        rx_match = re.search(r"Total Packets Received:\s+(\d+)", output)
        lat_match = re.search(r"Average Network Latency:\s+(\d+\.?\d*)", output)
        
        received = int(rx_match.group(1)) if rx_match else 0
        latency = float(lat_match.group(1)) if lat_match else 0.0
        
        details = f"Rx: {received}, Lat: {latency:.2f}"
        
        # assertions
        if received < expected_min_packets:
            return TestResult(name, False, f"Low Throughput. {details}. Expected > {expected_min_packets}")
        
        if max_latency and latency > max_latency:
            return TestResult(name, False, f"High Latency. {details}. Expected < {max_latency}")
            
        if "Simulation finished" not in output:
             return TestResult(name, False, "Simulation did not finish cleanly.")

        return TestResult(name, True, details)

    except subprocess.TimeoutExpired:
        return TestResult(name, False, f"Timeout after {TIMEOUT}s (Possible Deadlock)")
    except Exception as e:
        return TestResult(name, False, f"Exception: {str(e)}")

def run_path_test():
    name = "Path Validation (2x2 Mesh XY)"
    cmd = [BINARY, "--topology", "../configs/topologies/Mesh_XY.py", "--rows", "2", "--cols", "2", "--cycles", "20", "--test-mode", "--debug"]
    print(f"Running Test: {name}...")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT)
        output = result.stdout
        
        # We expect Node 0 -> Node 3 (0,0 -> 1,1)
        # Path: Router 0 (East) -> Router 1 (South) -> NI 3
        
        # Check Router 0 decision (East is usually port 1)
        r0_check = re.search(r"Router 0 .* Selected port 1", output)
        # Check Router 1 decision (South is usually port 2)
        r1_check = re.search(r"Router 1 .* Selected port 2", output)
        
        if r0_check and r1_check:
            return TestResult(name, True, "Correct XY Path Verified")
        else:
            return TestResult(name, False, "Path Deviation Detected\n" + output) # Print FULL output
            
    except Exception as e:
        return TestResult(name, False, str(e))

def main():
    tests = [
        # 1. Smoke Test (Basic Connectivity)
        {
            "name": "Smoke Test (2x2 Mesh, Low Load)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "2", "--cols", "2", "--cycles", "1000", "--rate", "0.01"],
            "min_pkts": 10, # Expect ~40 (4 nodes * 0.01 * 1000)
            "max_lat": 10.0
        },
        # 2. Routing Logic (Table Based on Mesh)
        {
            "name": "Routing Test (4x4 Mesh, Table Routing)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "4", "--cols", "4", "--cycles", "1000", "--rate", "0.05", "--routing", "0"],
            "min_pkts": 500, # 16 * 0.05 * 1000 = 800
            "max_lat": 15.0
        },
        # 3. High Load / Saturation (Stress Test)
        {
            "name": "Saturation Test (4x4 Mesh, High Load)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "4", "--cols", "4", "--cycles", "5000", "--rate", "0.5"],
            "min_pkts": 10000, # Should be high throughput
            "max_lat": 100.0 # Queueing delay will be high, but should not crash
        },
        # 4. Multi-Flit Packets (Flow Control)
        {
            "name": "Multi-Flit Test (4x4 Mesh, 5-flit packets)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "4", "--cols", "4", "--cycles", "2000", "--rate", "0.05", "--packet-size", "5"],
            "min_pkts": 500,
            "max_lat": 25.0
        },
        # 5. Scalability (Large Network)
        {
            "name": "Scalability Test (8x8 Mesh)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "8", "--cols", "8", "--cycles", "2000", "--rate", "0.05"],
            "min_pkts": 5000, # 64 * 0.05 * 2000 = 6400
            "max_lat": 15.0
        },
        # 6. Alternate Topology (Crossbar)
        {
            "name": "Topology Test (Crossbar 16)",
            "args": ["--topology", "../configs/topologies/Crossbar.py", "--rows", "1", "--cols", "16", "--cycles", "1000", "--rate", "0.1", "--routing", "0"],
            "min_pkts": 1000,
            "max_lat": 10.0 # Should be low latency
        },
        # 7. Wormhole Stress Test (Packet Size > Buffer Size)
        # Buffers are 4 flits. Packet size 10 ensures flow control throttling.
        {
            "name": "Wormhole Stress (10-flit pkt, 4-flit buf)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "4", "--cols", "4", "--cycles", "2000", "--rate", "0.05", "--packet-size", "10"],
            "min_pkts": 500,
            "max_lat": 50.0 # Latency will be higher due to serialization
        },
        # 8. Zero Load Test (Idle)
        {
            "name": "Zero Load Test (Idle)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "2", "--cols", "2", "--cycles", "1000", "--rate", "0.0"],
            "min_pkts": 0,
            "max_lat": 0.0 # Irrelevant
        }
    ]

    results = []
    print("=== Garnet Standalone Production Test Suite ===\n")
    
    # Ensure binary exists
    if not os.path.exists(BINARY):
        print(f"Error: Binary {BINARY} not found. Build it first.")
        sys.exit(1)

    # Run Path Test First
    path_res = run_path_test()
    results.append(path_res)
    print(f"  Result: {'PASS' if path_res.success else 'FAIL'} ({path_res.details})\n")

    for t in tests:
        res = run_test(t["name"], t["args"], t.get("min_pkts", 0), t.get("max_lat"))
        results.append(res)
        print(f"  Result: {'PASS' if res.success else 'FAIL'} ({res.details})\n")

    print("=== Summary ===")
    passed = 0
    for r in results:
        status = "PASS" if r.success else "FAIL"
        print(f"[{status}] {r.name}")
        if not r.success:
            print(f"    Details: {r.details}")
        else:
            passed += 1
    
    print(f"\nPassed {passed}/{len(results)} tests.")
    
    if passed == len(results):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
