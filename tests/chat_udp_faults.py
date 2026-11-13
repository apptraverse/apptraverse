"""Deterministic faults on an owned UDP proxy, never on the system network."""
import asyncio

from udp_fault_proxy import UdpFaultProxy


class ChaosUdpProxy(UdpFaultProxy):
    def __init__(self, side, journal):
        super().__init__(side, journal)
        self.chaos_enabled = False
        self.ordinal = {"out": 0, "in": 0}
        self.highest = {"out": 0, "in": 0}
        self.chaos = {f"{direction}_{kind}": 0 for direction in ("out", "in")
                      for kind in ("dropped", "duplicated", "delayed", "reordered")}
        self.delayed = set()

    async def start(self):
        await super().start()
        original = self.transport
        owner = self

        class IncomingTransport:
            def sendto(self, data, address):
                owner.shape("in", lambda: original.sendto(data, address))

            def close(self):
                original.close()

        self.transport = IncomingTransport()
        return self

    def datagram_received(self, data, address):
        # Six-byte relay setup must reach the base class exactly once.
        if address not in self.clients:
            super().datagram_received(data, address)
        else:
            self.shape("out", lambda: super(ChaosUdpProxy, self).datagram_received(data, address))

    def shape(self, direction, forward):
        if not self.chaos_enabled:
            forward()
            return
        self.ordinal[direction] += 1
        ordinal = self.ordinal[direction]
        if ordinal % 5 == 0:
            self.chaos[direction + "_dropped"] += 1
            return

        def emit():
            if not self.gate.is_set():
                self.dropped[direction] += 1
                return
            if ordinal < self.highest[direction]:
                self.chaos[direction + "_reordered"] += 1
            self.highest[direction] = max(ordinal, self.highest[direction])
            forward()

        def schedule(delay):
            handle = None

            def run():
                self.delayed.discard(handle)
                emit()

            handle = asyncio.get_running_loop().call_later(delay, run)
            self.delayed.add(handle)

        if ordinal % 3 == 0:
            self.chaos[direction + "_delayed"] += 1
            schedule(.15)
        else:
            emit()
        if ordinal % 7 == 0:
            self.chaos[direction + "_duplicated"] += 1
            schedule(.005)

    def enable_chaos(self, enabled):
        self.chaos_enabled = enabled
        if not enabled:
            for handle in self.delayed:
                handle.cancel()
            self.delayed.clear()
        self.journal.log("chaos", side=self.side, enabled=enabled, counters=dict(self.chaos))

    async def close(self):
        self.enable_chaos(False)
        await super().close()
