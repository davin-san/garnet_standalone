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
    # Use --trace-packet to verify coordinates and directions
    cmd = [BINARY, "--topology", "Mesh_XY", "--rows", "2", "--cols", "2", "--cycles", "20", "--test-mode", "--trace-packet"]
    print(f"Running Test: {name}...")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT)
        output = result.stdout
        
        # We expect NI 0 -> NI 3 (0,0 -> 1,1)
        # Path: Router 0 (East) -> Router 1 (South) -> NI 3
        
        r0_check = re.search(r"DEPARTING from Router 0 \(0,0,0\) via port East", output)
        r1_check = re.search(r"DEPARTING from Router 1 \(1,0,0\) via port South", output)
        
        if r0_check and r1_check:
            return TestResult(name, True, "Correct Coordinate-aware XY Path Verified")
        else:
            return TestResult(name, False, "Path Deviation Detected or Trace Missing\n" + output)
            
    except Exception as e:
        return TestResult(name, False, str(e))

def run_3d_path_test():
    name = "3D Coordinate Validation (2x2x2 Mesh XYZ)"
    # NI 0 (0,0,0) -> NI 7 (1,1,1)
    cmd = [BINARY, "--topology", "../configs/topologies/Mesh_3D.py", "--rows", "2", "--cols", "2", "--depth", "2", "--cycles", "50", "--test-mode", "--trace-packet", "--routing", "1"]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT)
        output = result.stdout
        
        expected_hops = [
            r"INJECTED at NI 0 \(0,0,0\)",
            r"DEPARTING from Router 0 \(0,0,0\) via port East",
            r"ARRIVED at Router 1 \(1,0,0\) at port West",
            r"DEPARTING from Router 1 \(1,0,0\) via port South",
            r"ARRIVED at Router 3 \(1,1,0\) at port North",
            r"DEPARTING from Router 3 \(1,1,0\) via port Up",
            r"ARRIVED at Router 7 \(1,1,1\) at port Down",
            r"EJECTED at NI 7 \(1,1,1\)"
        ]
        
        missing = []
        for hop in expected_hops:
            if not re.search(hop, output):
                missing.append(hop)
        
        if not missing:
            return TestResult(name, True, "3D Path Verified (X->Y Order)")
        else:
            return TestResult(name, False, f"Missing hops: {', '.join(missing)}")
            
    except Exception as e:
        return TestResult(name, False, str(e))

def main():
    tests = [
        # 1. Smoke Test (Basic Connectivity)
        {
            "name": "Smoke Test (2x2 Mesh, Low Load)",
            "args": ["--topology", "Mesh_XY", "--rows", "2", "--cols", "2", "--cycles", "1000", "--rate", "0.01"],
            "min_pkts": 10,
            "max_lat": 10.0
        },
        # 2. Routing Logic (Table Based on Mesh - using Python topology to test conf_generator)
        {
            "name": "Routing Test (4x4 Mesh, Table Routing)",
            "args": ["--topology", "../configs/topologies/Mesh_XY.py", "--rows", "4", "--cols", "4", "--cycles", "1000", "--rate", "0.05", "--routing", "0"],
            "min_pkts": 500,
            "max_lat": 20.0
        },
        # 3. High Load / Saturation (Stress Test)
        {
            "name": "Saturation Test (4x4 Mesh, High Load)",
            "args": ["--topology", "Mesh_XY", "--rows", "4", "--cols", "4", "--cycles", "5000", "--rate", "0.5"],
            "min_pkts": 5000, 
            "max_lat": 100.0 
        },
        # 4. Multi-Flit Packets (Flow Control)
        {
            "name": "Multi-Flit Test (4x4 Mesh, 5-flit packets)",
            "args": ["--topology", "Mesh_XY", "--rows", "4", "--cols", "4", "--cycles", "2000", "--rate", "0.05", "--packet-size", "5"],
            "min_pkts": 500,
            "max_lat": 30.0
        },
        # 5. Scalability (Large Network)
        {
            "name": "Scalability Test (8x8 Mesh)",
            "args": ["--topology", "Mesh_XY", "--rows", "8", "--cols", "8", "--cycles", "2000", "--rate", "0.05"],
            "min_pkts": 5000, 
            "max_lat": 20.0
        },
        # 6. Alternate Topology (Pt2Pt)
        {
            "name": "Topology Test (Pt2Pt 16)",
            "args": ["--topology", "../configs/topologies/Pt2Pt.py", "--rows", "1", "--cols", "16", "--cycles", "1000", "--rate", "0.1", "--routing", "0"],
            "min_pkts": 1000,
            "max_lat": 10.0 
        },
        # 7. Wormhole Stress Test (Packet Size > Buffer Size)
        # Buffers are 4 flits. Packet size 10 ensures flow control throttling.
        {
            "name": "Wormhole Stress (10-flit pkt, 4-flit buf)",
            "args": ["--topology", "Mesh_XY", "--rows", "4", "--cols", "4", "--cycles", "2000", "--rate", "0.05", "--packet-size", "10"],
            "min_pkts": 500,
            "max_lat": 50.0 # Latency will be higher due to serialization
        },
        # 8. Zero Load Test (Idle)
        {
            "name": "Zero Load Test (Idle)",
            "args": ["--topology", "Mesh_XY", "--rows", "2", "--cols", "2", "--cycles", "1000", "--rate", "0.0"],
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

    # Run 3D Path Test
    path_3d_res = run_3d_path_test()
    results.append(path_3d_res)
    print(f"  Result: {'PASS' if path_3d_res.success else 'FAIL'} ({path_3d_res.details})\n")

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
