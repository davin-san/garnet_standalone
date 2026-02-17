import sys
import os
import argparse
import importlib
import heapq

# Setup paths
current_dir = os.path.dirname(os.path.abspath(__file__))
gem5_root = os.path.abspath(os.path.join(current_dir, "../../"))
configs_dir = os.path.join(gem5_root, "configs")
topologies_dir = os.path.join(configs_dir, "topologies")

sys.path.insert(0, current_dir)
sys.path.insert(0, configs_dir)
sys.path.insert(0, topologies_dir)

# Import m5 mocks
import m5
import m5.objects
from m5.objects import IntLink, ExtLink, Router

class MockNetwork:
    def __init__(self):
        self.routers = []
        self.ext_links = []
        self.int_links = []

class MockOptions:
    def __init__(self, rows, cols, mem_size="4GB"):
        self.mesh_rows = rows
        self.mesh_cols = cols
        self.num_cpus = rows * cols
        self.mem_size = mem_size
        self.link_latency = 1
        self.router_latency = 1
        self.network = "garnet"

class MockController:
    def __init__(self, id, type="Directory_Controller"):
        self.type = type
        self.id = id

def generate_conf(topology_name, rows, cols, num_cpus=None):
    output_file = "topology.conf"
    
    # 1. Load Topology Class
    if topology_name.endswith('.py'):
        # Normalize path and extract filename/directory
        topo_abs_path = os.path.abspath(topology_name)
        module_name = os.path.basename(topo_abs_path).replace('.py', '')
        
        import importlib.util
        spec = importlib.util.spec_from_file_location(module_name, topo_abs_path)
        topo_module = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(topo_module)
            topo_class = getattr(topo_module, module_name)
        except Exception as e:
            print(f"Error loading topology from {topo_abs_path}: {e}")
            sys.exit(1)
    else:
        # Assume it's a topology name within configs/topologies/
        try:
            topo_module = importlib.import_module(f"topologies.{topology_name}")
            topo_class = getattr(topo_module, topology_name)
        except (ImportError, AttributeError) as e:
            print(f"Error importing topology {topology_name}: {e}")
            sys.exit(1)
        
    print(f"Generating {output_file} using class {topo_class.__name__} for {rows}x{cols}")

    # 2. Build Mock Network
    options = MockOptions(rows, cols)
    if num_cpus:
        options.num_cpus = num_cpus

    controllers = [MockController(i) for i in range(options.num_cpus)]
    topo = topo_class(controllers)
    network = MockNetwork()
    topo.makeTopology(options, network, IntLink, ExtLink, Router)

    # Adjacency list: router_id -> list of (neighbor_router_id, port_index)
    adj = {r.router_id: [] for r in network.routers}
    # Direct NI connections: router_id -> list of (ni_id, port_index)
    router_to_nis = {r.router_id: [] for r in network.routers}

    # 3. Populate connectivity
    # External Links (NI <-> Router)
    for i, link in enumerate(network.ext_links):
        r_id = link.int_node.router_id
        ni_id = link.ext_node.id
        pass

    # Actually, we need to replicate the EXACT port indexing used in C++ Topology.cc/FileTopology.cc
    # In FileTopology.cc:
    #   ExtLinks are added first. Link ID base starts at 0.
    #   Each ExtLink takes 1 port at router.
    #   Internal Links follow.
    
    # Let's count ports per router
    router_ports = {r.router_id: [] for r in network.routers}
    
    for link in network.ext_links:
        r_id = link.int_node.router_id
        router_ports[r_id].append({'type': 'ext', 'id': link.ext_node.id})
        
    for link in network.int_links:
        src = link.src_node.router_id
        router_ports[src].append({'type': 'int', 'id': link.dst_node.router_id})

    # Now we have port indices (the index in router_ports[r_id])
    routing_tables = {r.router_id: {} for r in network.routers}

    for start_r in network.routers:
        sid = start_r.router_id
        pq = [] # (distance, current_router, first_hop_port)
        distances = {r.router_id: float('inf') for r in network.routers}
        distances[sid] = 0
        
        # Initial: neighbors of sid
        for port_idx, port in enumerate(router_ports[sid]):
            if port['type'] == 'ext':
                routing_tables[sid][port['id']] = port_idx
            else:
                neighbor_id = port['id']
                if 1 < distances[neighbor_id]:
                    distances[neighbor_id] = 1
                    heapq.heappush(pq, (1, neighbor_id, port_idx))

        while pq:
            dist, u, first_hop = heapq.heappop(pq)
            if dist > distances[u]: continue
            
            for port_idx, port in enumerate(router_ports[u]):
                if port['type'] == 'ext':
                    ni_id = port['id']
                    if ni_id not in routing_tables[sid]:
                        routing_tables[sid][ni_id] = first_hop
                else:
                    v = port['id']
                    if dist + 1 < distances[v]:
                        distances[v] = dist + 1
                        heapq.heappush(pq, (distances[v], v, first_hop))
        
    # 4. Write Output
    with open(output_file, 'w') as f:
        print(f"# Topology: {topology_name}", file=f)
        
        # Calculate dimensions for coordinates
        x_dim = cols
        y_dim = rows
        z_dim = len(network.routers) // (x_dim * y_dim)
        if z_dim == 0: z_dim = 1

        print(f"NumRouters {len(network.routers)}", file=f)
        for r in network.routers:
            rid = r.router_id
            rx = rid % x_dim
            ry = (rid // x_dim) % y_dim
            rz = rid // (x_dim * y_dim)
            print(f"{rid} {rx} {ry} {rz}", file=f)

        print(f"NumNIs {len(controllers)}", file=f)
        for c in controllers:
            cid = c.id
            cx = cid % x_dim
            cy = (cid // x_dim) % y_dim
            cz = cid // (x_dim * y_dim)
            print(f"{cid} {cx} {cy} {cz}", file=f)

        print(f"NumExtLinks {len(network.ext_links)}", file=f)
        print(f"NumIntLinks {len(network.int_links)}", file=f)
        
        print("\nExtLinks", file=f)
        for link in network.ext_links:
            print(f"{link.ext_node.id} {link.int_node.router_id}", file=f)
            
        print("\nIntLinks", file=f)
        for link in network.int_links:
            lat = 0 # Match gem5 network latency reporting
            weight = link.weight if link.weight is not None else 1
            src_p = link.src_outport if link.src_outport is not None else "None"
            dst_p = link.dst_inport if link.dst_inport is not None else "None"
            print(f"{link.src_node.router_id} {link.dst_node.router_id} {lat} {weight} {src_p} {dst_p}", file=f)

        print("\nRoutingTables", file=f)
        for r_id in sorted(routing_tables.keys()):
            for ni_id in sorted(routing_tables[r_id].keys()):
                print(f"{r_id} {ni_id} {routing_tables[r_id][ni_id]}", file=f)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--topology", required=True)
    parser.add_argument("--rows", type=int, default=2)
    parser.add_argument("--cols", type=int, default=2)
    parser.add_argument("--num-cpus", type=int, default=None)
    args = parser.parse_args()
    generate_conf(args.topology, args.rows, args.cols, args.num_cpus)