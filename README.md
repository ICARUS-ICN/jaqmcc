Simulation Scenarios for a Joint AQM and Congestion Control Algorithm
===

[![DOI](https://zenodo.org/badge/595252721.svg)](https://zenodo.org/doi/10.5281/zenodo.10925813)

This is the supporting code and simulation scenarios described in [End-to-end
Active Queue Management with Named-Data
Networking](https://netlab.det.uvigo.es/publication/e2e-aqm-ndn-23/).

Usage
---
Follow the instructions of the [INSTALL.md](INSTALL.md) file to install the
[ndnSIM](https://ndnsim.net) simulator. Then you will be able to run the
simulations.


Available simulations
=====================

Linear Simple
---------------

A Dumb-bell topology with a configurable number of consumers. All the consumers
share the same bottleneck, but can otherwise keep the queue bounded thanks to
the CoDel implementation run by the consumer applications.

Cascade Simple
---------------

A similar scenario to the previous one, but this time there are various
bottlenecks that change depending on the actual active consumer applications.

---
### Legal:
Copyright ⓒ 2021–2023 Universidade de Vigo<br>
Author: Miguel Rodríguez Pérez <miguel@det.uvigo.gal><br>
This software is licensed under the GNU General Public License, version 3 (GPL-3.0) or later. For information see LICENSE.

Project PID2020-113240RB-I00 financed by MCIU/AEI/10.13039/501100011033.
![MCIU-AEI Logo](https://icarus.det.uvigo.es/assets/img/logo-mcin-aei.jpeg)

