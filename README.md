# Garnet Standalone NoC Simulator

A high-performance, lightweight Network-on-Chip (NoC) simulator with full feature parity to gem5 Garnet. This tool is designed for rapid architectural exploration, reliability analysis, and 3D visualization.

## Key Features
- **Perfect Parity with gem5:** Replicates Garnet's flit-level timing, switch allocation, and flow control.
- **3D Coordinate Awareness:** Native support for (X, Y, Z) router and NI positioning.
- **2.5D/3D Modeling:** Built-in SerDes and Clock Domain Crossing (CDC) modeling via `NetworkBridge`.
- **Fault Model:** Variation-induced fault modeling (data corruption, flit loss, etc.) based on router config and temperature.
- **Algorithmic & Table Routing:** Intelligent XY(Z) routing for meshes and Dijkstra-based table routing for custom topologies.
- **Packet Tracing:** High-precision hop-by-hop tracing for debugging and visualization.

## Building
```bash
make clean
make
```

## Usage
```bash
./garnet_standalone [options]
```

### Common Options:
- `--topology <name>`: Path to a Python topology file or a built-in name (e.g., `Mesh_XY`).
- `--rows <int>`, `--cols <int>`: Dimensions for mesh topologies.
- `--routing <0|1>`: `0` for Table-based, `1` for Algorithmic XY(Z).
- `--rate <float>`: Injection rate (flits/cycle/node).
- `--packet-size <int>`: Number of flits per packet.
- `--fault-model`: Enable the variation-induced fault model.
- `--trace-packet`: Enable detailed flit-level path tracing.
- `--cycles <int>`: Simulation duration.

## 3D Coordinates
The simulator uses a coordinate system mapped as:
`ID = x + y*cols + z*(rows*cols)`

Coordinates are printed in traces as `(X, Y, Z)` and are used by the Algorithmic XYZ routing to make dimension-order decisions.

## Tests
A production test suite is included to verify accuracy and performance:
```bash
python3 production_test.py
```