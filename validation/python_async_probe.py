"""Drive the Python SDK's real L6 codec/facade with its reusable CAN fixture.

No Python SDK code is copied or changed. --python-sdk must name a source checkout
containing src/ and tests/; install its dependencies in the selected interpreter.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def probe(sdk: Path, scenario: str, capacity: int, targets: int, timeout_ms: int):
    sys.path.insert(0, str(sdk))
    sys.path.insert(0, str(sdk / "src"))
    sys.path.insert(0, str(sdk.parent / "linkerbot-transport" / "src"))
    from linkerbot_hand import (HandDevice, HandSide, DeviceConfig, CommandStage,
                               ValidationError, UnsupportedOperationError,
                               ClosedError, DeviceNotReadyError, QueueFullError,
                               TimeoutError, PollingPlan, AngleFeedbackPolicy)
    from tests.hand.api_v1.runtime.classic_can_fake import (
        ScriptedClassicCANTransport, make_script)

    script = make_script("L6", HandSide.LEFT)

    class Transport(ScriptedClassicCANTransport):
        def __init__(self):
            super().__init__(script)
            self.gate = threading.Event()
            self.gate.set()
            self.entered = threading.Event()
            self.ack_request = threading.Event()
            self.polled = threading.Event()
            self.partial = False

        def send(self, message):
            data = bytes(message.data)
            if len(data) > 1 and data[0] in (1, 6):
                self.entered.set()
                if not self.gate.wait(4):
                    raise RuntimeError("test did not release send barrier")
            if data == b"\x01":
                self.polled.set()
            if len(data) > 1 and data[0] == 6 and self.partial:
                # Prevent the fixture's automatic full reply, but emit a short one.
                from linkerbot_transport.testing import FakeTransport
                receipt = FakeTransport.send(self, message)
                for _ in script.respond(message):
                    pass
                if scenario != "execution_timeout":
                    self.emit(script.frame(b"\x06\x01"))
                self.ack_request.set()
                return receipt
            return super().send(message)

    transport = Transport()
    hand = HandDevice(model="L6", side=HandSide.LEFT, transport=transport,
                      config=DeviceConfig(discrete_queue_capacity=capacity,
                          transaction_timeout_s=timeout_ms / 1000 if scenario == "execution_timeout" else 1))
    hand.open()

    def complete(handle):
        stage = handle.wait(4)
        assert handle.error is None, handle.error
        assert stage in (CommandStage.DISPATCHED, CommandStage.ACKNOWLEDGED), stage
        return stage.value

    def block():
        transport.entered.clear()
        transport.gate.clear()
        handle = hand.set_angles((10,) * 6)
        assert transport.entered.wait(2), "send barrier not entered"
        assert not handle.done, "setter waited for transport"
        return handle

    try:
        if scenario == "admission_invalid":
            before = len(script.sent_payloads)
            for action, error in ((lambda: hand.set_angles((1,)), ValidationError),
                                  (lambda: hand.set_accelerations((1,) * 6), UnsupportedOperationError)):
                try:
                    action()
                except error:
                    pass
                else:
                    raise AssertionError("invalid/unsupported setter accepted")
            assert len(script.sent_payloads) == before
            hand.close()
            try:
                hand.set_angles((10,) * 6)
            except (ClosedError, DeviceNotReadyError):
                pass
            else:
                raise AssertionError("closed facade accepted setter")
            return {"rejected": True, "frames": 0}
        if scenario == "dispatch_without_ack":
            handle = hand.set_angles((targets,) * 6)
            return {"stage": complete(handle), "error": None}
        if scenario in ("ack_after_complete_response", "execution_timeout"):
            transport.partial = True
            handle = hand.set_position_kp((targets,) * 6)
            assert transport.ack_request.wait(2)
            if scenario == "execution_timeout":
                assert handle.wait(2) is CommandStage.FAILED
                assert handle.error.code.value == "timeout", handle.error
                return {"stage": "failed", "exception": "timeout", "budget": "response_window"}
            assert handle.wait(1) is CommandStage.FAILED
            assert handle.error.code.value == "protocol", handle.error
            transport.partial = False
            # A malformed matching response is a protocol failure in Python.
            # A separate request still accepts a complete, unequal reply as ACK.
            script.bad_command_echoes[6] = (1,) * 6
            complete_handle = hand.set_position_kp((targets,) * 6)
            return {"stage": complete(complete_handle), "partial_terminal": True,
                    "partial_stage": "failed+protocol", "error": None}
        if scenario in ("latest_pending_replaced", "claimed_request_not_replaced"):
            claimed = block()
            old = hand.set_angles((targets,) * 6)
            newest = hand.set_percent_angles((50,) * 6)
            assert old.stage is CommandStage.SUPERSEDED and old.sent_frames == 0
            assert not claimed.done
            transport.gate.set()
            stage = complete(claimed)
            complete(newest)
            if scenario == "latest_pending_replaced":
                return {"stage": "superseded", "sent_frames": 0}
            return {"stage": stage, "superseded": False}
        if scenario == "fifo_full":
            claimed = block()
            pending = [hand.set_position_kp((i + 1,) * 6) for i in range(capacity)]
            before = sum(family.accepted for family in hand.get_health().command_families)
            try:
                hand.set_position_kp((20,) * 6)
            except QueueFullError:
                pass
            else:
                raise AssertionError("FIFO was not bounded")
            assert sum(family.accepted for family in hand.get_health().command_families) == before
            transport.gate.set()
            complete(claimed)
            for handle in pending:
                complete(handle)
            values = [data[1] for data in script.command_payloads if data[0] == 6]
            assert values == list(range(1, capacity + 1)), values
            return {"exception": "queue_full", "accepted_count_unchanged": True, "ordered": True}
        if scenario == "wait_timeout":
            handle = block()
            try:
                handle.wait(timeout_ms / 1000)
            except TimeoutError:
                pass
            else:
                raise AssertionError("caller wait unexpectedly completed")
            assert not handle.done and handle.error is None
            transport.gate.set()
            return {"wait": "TimeoutError", "command_cancelled": False, "stage": complete(handle)}
        if scenario == "lifecycle_invalidation":
            claimed = block()
            pending = hand.set_position_kp((targets,) * 6)
            with ThreadPoolExecutor(max_workers=1) as executor:
                closer = executor.submit(hand.close)
                try:
                    assert pending.wait(1) is CommandStage.FAILED
                    assert pending.error.code.value == "closed", pending.error
                finally:
                    transport.gate.set()
                closer.result(3)
            assert complete(claimed) == "dispatched"
            script.command_payloads.clear()
            hand.open()
            assert script.command_payloads == [], "old configuration replayed"
            return {"pending": "failed+closed", "claimed": "dispatched", "replay": False}
        if scenario == "polling_fairness":
            claimed = block()
            with ThreadPoolExecutor(max_workers=1) as executor:
                cached = executor.submit(lambda: (hand.get_snapshot(), hand.get_health()))
                try:
                    cached.result(0.5)
                    transport.polled.clear()
                    hand.start_polling(PollingPlan(angles_hz=50, angle_feedback=AngleFeedbackPolicy.QUERY_ONLY))
                    for i in range(targets):
                        hand.set_angles((i,) * 6)
                finally:
                    transport.gate.set()
            complete(claimed)
            assert transport.polled.wait(2), "polling starved"
            hand.stop_polling()
            return {"cached_reads_blocked": False, "poll_progress": True}
        raise ValueError(f"unknown vector scenario: {scenario}")
    finally:
        transport.gate.set()
        hand.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-sdk", type=Path, required=True)
    parser.add_argument("scenario")
    parser.add_argument("capacity", type=int)
    parser.add_argument("targets", type=int)
    parser.add_argument("timeout_ms", type=int)
    args = parser.parse_args()
    print(json.dumps(probe(args.python_sdk, args.scenario, args.capacity, args.targets, args.timeout_ms)))
