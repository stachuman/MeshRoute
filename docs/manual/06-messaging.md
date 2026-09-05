# Messaging

> Status: Skeleton. Message syntax and delivery evidence still require source and metal-evidence review.

## Before you begin

<!-- Document identity, routing, and key prerequisites here. -->

## Send a direct message

A direct message carries at most **232 bytes** of body. The limit reserves room inside the 241-byte frame payload
for the identity fields that make the message verifiable: the recipient key hash (4 bytes), routing origin
(1 byte), and sender key hash (4 bytes). MeshRoute never drops an available identity field to squeeze in a longer
body; it refuses a longer message immediately with `err_too_large`, before transmitting it.

A first-contact message delegated through a mobile's home also carries a one-byte enclosed-type marker, so that
specific carrier accepts at most **231 bytes**. This is a carrier boundary, not a second general DM limit.

## Send a channel message

<!-- Add verified channel and scope workflows here. -->

## Encryption and location options

<!-- Document only implemented, operator-visible combinations and refusals. -->

## Confirm the result

<!-- Explain accepted-for-send evidence separately from received/delivered evidence. -->

## Common messaging errors

<!-- Add current errors and corrective actions here. -->

## Next step

Continue to [Inbox](07-inbox.md).
