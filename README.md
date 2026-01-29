# Garnet Standalone Simulator

This is a lightweight, standalone version of the **Garnet Network-on-Chip (NoC)** model derived from gem5. It allows for high-speed, cycle-accurate network simulations without the overhead of the full gem5 infrastructure.

## Features
- **Cycle-Accurate**: Matches gem5's micro-architecture (Flits, VCs, Switch Allocators, etc.) exactly.
- **Lightweight**: <10MB memory usage, negligible startup time.
- **Modular Topology**: Easy to extend `Topology` class for custom layouts.
- **Configurable**: Runtime arguments for grid size, traffic rates, and routing algorithms.

## Building
To build the simulator, simply run make in this directory:

```bash
make clean
make
```

This produces the `garnet_standalone` binary.

## Running Simulations

The simulator accepts standard command-line arguments.

### Deterministic Test (Verification)
Runs a single packet from Node 0 to Node 3 to verify latency and connectivity.

```bash
./garnet_standalone --test-mode --rows 2 --cols 2 --cycles 100
```

### Random Traffic Simulation
Runs a uniform random traffic pattern.

```bash
# 4x4 Mesh, 5% injection rate, 10,000 cycles, XY Routing
./garnet_standalone --rows 4 --cols 4 --rate 0.05 --cycles 10000 --routing 1
```

## Command Line Options

| Option | Argument | Default | Description |
| :--- | :--- | :--- | :--- |
| `--rows` | `<int>` | 2 | Number of rows in the mesh. |
| `--cols` | `<int>` | 2 | Number of columns in the mesh. |
| `--cycles` | `<int>` | 1000 | Total simulation cycles to run. |
| `--rate` | `<float>` | 0.01 | Injection rate (flits/cycle/node). |
| `--packet-size` | `<int>` | 1 | Size of packet in flits. |
| `--routing` | `<int>` | 1 | 0 = Table-based (Not impl), 1 = XY Routing. |
| `--test-mode` | (Flag) | Off | Enable deterministic single-packet test (0->3). |
| `--help` | (Flag) | - | Show usage summary. |

## Source Structure
- `src/main.cc`: Simulation driver and argument parsing.
- `src/Topology.cc`: Network construction factory (Mesh connection logic).
- `src/GarnetNetwork.cc`: Central network manager.
- `src/Router.cc`: Core router logic (Routing, Switch Allocation).
- `src/NetworkInterface.cc`: Traffic injection/ejection logic.

## Adding New Topologies
1. Define a new class in `src/Topology.hh` inheriting from `Topology`.
2. Implement the `build()` method to instantiate routers and call `connectRouters()`.
3. Add the new name to `Topology::create()` in `src/Topology.cc`.
