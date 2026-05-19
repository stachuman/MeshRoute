  ## What Is MeshRoute?

  MeshRoute is a proposal for an advanced communication protocol and tool built on LoRa technology.

  The core idea is to create a fully decentralized and scalable network that treats radio resources as scarce, valuable, and worth protecting. MeshRoute aims to
  minimize unnecessary transmissions while keeping communication practical in real-world, dense deployments.

  ## Why MeshRoute?

  MeshRoute was born out of frustration with the way existing LoRa mesh solutions struggle as interest and network density grow.

  Projects such as MeshCore and Meshtastic are impressive and valuable, but in dense deployments their limits become visible quickly: they consume available
  airtime, put heavy pressure on duty cycle limits, which in Europe are often restricted to 1%, and expose the practical inefficiency of flood-based routing.

  MeshRoute takes a different direction. Instead of relying on flooding, it focuses on deliberate routing and adapts protocol parameters to observed network
  conditions, including link quality between individual nodes and automatic spreading factor selection.

  This is a trade-off: the protocol aims to use spectrum more carefully, improve message delivery efficiency, reduce unnecessary network load, and avoid collisions,
  sometimes at the cost of higher latency. For dense and realistic deployments, this trade-off is intentional.

  MeshRoute is an early but complete proposal for a protocol and tool built around this idea. I believe it is a promising direction for creating an efficient,
  reliable, and practical communication medium for enthusiasts, amateur radio operators, experimenters, early adopters, and people looking for resilient emergency
  communication.

  ## Can I Use It Now?

  The protocol design is ready, and the first firmware builds are expected soon. More details will be shared as the implementation becomes available.


