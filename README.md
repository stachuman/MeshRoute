  ## What Is MeshRoute?

  MeshRoute is a proposal for an advanced communication protocol and tool built on LoRa technology.

  The core idea is to create a fully decentralized and scalable network that treats radio resources as scarce, valuable, and worth protecting. MeshRoute aims to minimize unnecessary transmissions while keeping communication practical in real-world, dense deployments.

  > 📖 **New here? [How MeshRoute works →](docs/how-it-works.md)** — a short, diagram-led walkthrough of how a message actually travels.

  > 📖 **Do you want to start from details? [Frame details →](docs/frames.md)** — comprehensive walk-through frames used in protocol (protocol rules will come soon).

  > 🛠️ **Working on the code? [Architecture →](ARCHITECTURE.md)** — the source-module map: where everything lives and how the pieces fit.

  ## Why MeshRoute?

  MeshRoute was born out of frustration with the way existing LoRa mesh solutions struggle as interest and network density grow.

  Projects such as MeshCore and Meshtastic are impressive and valuable, but in dense deployments their limits become visible quickly: they consume available
  airtime, put heavy pressure on duty cycle limits, which in Europe are often restricted to 1%, and expose the practical inefficiency of flood-based routing.

  MeshRoute takes a different direction. Instead of relying on flooding, it focuses on deliberate routing and adapts protocol parameters to observed network conditions, including link quality between individual nodes and automatic spreading factor selection.

  This is a trade-off: the protocol aims to use spectrum more carefully, improve message delivery efficiency, reduce unnecessary network load, and avoid collisions,
  sometimes at the cost of higher latency. For dense and realistic deployments, this trade-off is intentional.

  MeshRoute is an early but complete proposal for a protocol and tool built around this idea. I believe it is a promising direction for creating an efficient,
  reliable, and practical communication medium for enthusiasts, amateur radio operators, experimenters, early adopters, and people looking for resilient emergency
  communication.

  ## MeshRoute Principles

  The main weakness of many existing LoRa mesh solutions is the lack of deliberate routing. Flood-based communication is simple and robust at small scale, but in dense networks it quickly overuses shared airtime and overloads individual nodes.

  MeshRoute takes a different approach: it builds and maintains routing knowledge, but does so lazily and carefully. This matters because LoRa networks operate under severe bandwidth limits, and nodes cannot be assumed to be permanently available. Devices may go offline, move, lose power, change link quality, or join the
  network later.

  The protocol is therefore designed around a lightweight, self-adapting routing table. Routes are learned gradually, refreshed only when useful, and adjusted as
  physical conditions change, such as SNR degradation, duty-cycle pressure, node disappearance, or topology changes.

  MeshRoute splits the network into layers. A local layer can contain up to roughly 250 nodes and operates with its own control spreading factor and one or more data spreading factors. Control traffic and data traffic can therefore be tuned separately, allowing the network to balance reach, airtime cost, and reliability.

  Layers can be connected through gateways. A gateway participate in two layers and forward traffic between them according to an explicitly defined layer path. This makes it possible to scale beyond a single local mesh without turning the entire network into one large flood domain.

  Inside a single layer, communication can use a node’s short local ID, short public-key hash, or full public identity depending on context. Across layers, messages are addressed using the destination public identity together with a defined path through gateway-connected layers.

  ## Can I Use It Now?

There has been a significant progress since starting the project beginning of the year - which, btw. started by building LoRa simulator to build a right environment for testing algorithms.

Nowadays the core components are working and I'm at the stage of fixing functionality - through tests on a real hardware. That will continue for next 2 months, before creating first release.

On top - a simple iPhone application - to interact with node is being created - it will be released along with the first official release.

  
  ## Who Wrote It?

  My name is Stanislaw Kozicki, and I am the author of the MeshRoute protocol and implementation.

  During development I use automated tools to support implementation, analysis, and iteration. To be clear, the protocol design and engineering decisions are reviewed by me at every step. I have over 25 years of software development experience, and MeshRoute is developed with that level of scrutiny and care.
