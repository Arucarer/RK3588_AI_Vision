#!/usr/bin/env python3

import argparse
import json
import socket
import socketserver
import sqlite3
import struct
import time
import base64
import hashlib
import math
MAX_MESSAGE_SIZE = 12 * 1024 * 1024
MAX_JPEG_SIZE = 8 * 1024 * 1024


def recv_exact(sock, size):
    data = bytearray()

    while len(data) < size:
        block = sock.recv(size - len(data))

        if not block:
            raise EOFError("peer disconnected")

        data.extend(block)

    return bytes(data)
def validate_heartbeat(message):
    # 兼容旧版心跳：可以没有 timestamp_us 和 device_status。
    if "timestamp_us" in message:
        timestamp = message["timestamp_us"]

        if type(timestamp) is not int or not 0 <= timestamp <= 9223372036854775807:
            raise ValueError("invalid heartbeat timestamp_us")

    if "device_status" not in message:
        return

    status = message["device_status"]

    if not isinstance(status, dict):
        raise ValueError("device_status must be an object")

    # 带状态的新心跳必须携带采样时间。
    if "timestamp_us" not in message:
        raise ValueError("device_status requires timestamp_us")

    key = "max_thermal_zone_temperature_c"

    if key not in status:
        raise ValueError("missing temperature field")

    temperature = status[key]

    # null 表示板端没有读到有效温度。
    if temperature is None:
        return

    # 不接受 bool、字符串、非有限值或超出范围的数值。
    if type(temperature) not in (int, float):
        raise ValueError("temperature must be a number or null")

    if not -100 <= temperature <= 250:
        raise ValueError("temperature outside valid range")

    if not math.isfinite(temperature):
        raise ValueError("temperature must be finite")

def validate_message(message):
    if not isinstance(message, dict):
        raise ValueError("message must be an object")

    if message.get("version") != 1:
        raise ValueError("unsupported protocol version")

    if message.get("type") not in ("heartbeat", "detection"):
        raise ValueError("unsupported message type")

    for key in ("device_id", "message_id"):
        value = message.get(key)

        if (
            not isinstance(value, str)
            or not 1 <= len(value) <= 128
            or any(
                ch not in
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789_-"
                for ch in value
            )
        ):
            raise ValueError("invalid " + key)

    if message["type"] == "heartbeat":
        validate_heartbeat(message)
        return

    if message["type"] == "detection":
        boxes = message.get("boxes")

        if not isinstance(boxes, list) or len(boxes) > 100:
            raise ValueError("invalid boxes")

        for key in ("image_width", "image_height"):
            if type(message.get(key)) is not int or message[key] <= 0:
                raise ValueError("invalid " + key)

        if type(message.get("timestamp_us")) is not int:
            raise ValueError("invalid timestamp_us")

        for box in boxes:
            if not isinstance(box, dict):
                raise ValueError("invalid box")

            if type(box.get("class_id")) is not int:
                raise ValueError("invalid class_id")

            if not 0 <= box["class_id"] < 6:
                raise ValueError("invalid class_id")

            for key in ("confidence", "x1", "y1", "x2", "y2"):
                value = box.get(key)

                if type(value) not in (int, float):
                    raise ValueError("invalid box " + key)

            if not 0 <= box["confidence"] <= 1:
                raise ValueError("invalid confidence")

            if box["x2"] < box["x1"] or box["y2"] < box["y1"]:
                raise ValueError("invalid coordinates")


def reject_constant(value):
    raise ValueError("invalid JSON number: " + value)


def store_event(database, message):
    if message.get("version") != 1 or message.get("type") != "event":
        raise ValueError("invalid event envelope")

    metadata = message.get("metadata")
    if not isinstance(metadata, dict):
        raise ValueError("invalid event metadata")

    if metadata.get("schema_version") != 1:
        raise ValueError("unsupported event schema")

    detection = metadata.get("detection")
    validate_message(detection)

    if detection["type"] != "detection":
        raise ValueError("event requires detection data")

    event_id = metadata.get("event_id")

    if event_id != detection["message_id"]:
        raise ValueError("event/message ID mismatch")

    image_info = metadata.get("image")
    if not isinstance(image_info, dict):
        raise ValueError("invalid image metadata")

    if image_info.get("file") != "image.jpg":
        raise ValueError("unsupported image filename")

    for key in ("width", "height"):
        if type(image_info.get(key)) is not int or image_info[key] <= 0:
            raise ValueError("invalid image dimension")

    encoded = message.get("image_base64")
    if not isinstance(encoded, str):
        raise ValueError("missing image_base64")

    if len(encoded) > ((MAX_JPEG_SIZE + 2) // 3) * 4:
        raise ValueError("encoded image too large")

    jpeg = base64.b64decode(encoded, validate=True)

    if not 4 <= len(jpeg) <= MAX_JPEG_SIZE:
        raise ValueError("invalid JPEG size")

    # 基础文件格式检查；不代表已经完整解码验证 JPEG。
    if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
        raise ValueError("invalid JPEG markers")

    payload = json.dumps(
        metadata,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )

    digest = hashlib.sha256(jpeg).hexdigest()
    device = detection["device_id"]

    # 图片和元数据在同一个事务中保存，提交完成后才允许 ACK。
    with database:
        database.execute(
            """
            INSERT OR IGNORE INTO events
            (device_id, event_id, received_ns, metadata, image_sha256, jpeg)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (device, event_id, time.time_ns(), payload, digest, jpeg),
        )

        row = database.execute(
            """
            SELECT metadata, image_sha256
            FROM events
            WHERE device_id=? AND event_id=?
            """,
            (device, event_id),
        ).fetchone()

        if row != (payload, digest):
            raise ValueError("event ID already exists with different content")

    print(
        "event saved/confirmed:",
        device,
        event_id,
        "jpeg_bytes=",
        len(jpeg),
        flush=True,
    )

def update_temperature_alarm(
    database, message, temperature, received_ns, high, clear
):
    if temperature is None:
        return None

    device_id = message["device_id"]

    row = database.execute(
        """
        SELECT active
        FROM temperature_alarm_state
        WHERE device_id=?
        """,
        (device_id,),
    ).fetchone()

    was_active = bool(row[0]) if row is not None else False
    active = was_active

    if not was_active and temperature >= high:
        active = True
    elif was_active and temperature <= clear:
        active = False

    database.execute(
        """
        INSERT INTO temperature_alarm_state
            (device_id, active, temperature_c, updated_ns)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(device_id) DO UPDATE SET
            active=excluded.active,
            temperature_c=excluded.temperature_c,
            updated_ns=excluded.updated_ns
        """,
        (device_id, int(active), temperature, received_ns),
    )

    if active == was_active:
        return None

    transition = "raised" if active else "cleared"

    database.execute(
        """
        INSERT INTO temperature_alarm_events
        (
            device_id,
            message_id,
            received_ns,
            timestamp_us,
            transition,
            temperature_c,
            high_threshold_c,
            clear_threshold_c
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            device_id,
            message["message_id"],
            received_ns,
            message.get("timestamp_us"),
            transition,
            temperature,
            high,
            clear,
        ),
    )

    return transition
def store_heartbeat(database, message, high, clear):
    status = message.get("device_status", {})
    temperature = status.get("max_thermal_zone_temperature_c")

    payload = json.dumps(
        message,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )

    device_id = message["device_id"]
    message_id = message["message_id"]
    received_ns = time.time_ns()
    transition = None

    # 心跳、告警状态及告警记录在同一事务中提交。
    with database:
        cursor = database.execute(
            """
            INSERT OR IGNORE INTO heartbeats
            (
                device_id,
                message_id,
                received_ns,
                timestamp_us,
                max_thermal_zone_temperature_c,
                payload
            )
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                device_id,
                message_id,
                received_ns,
                message.get("timestamp_us"),
                temperature,
                payload,
            ),
        )

        inserted = cursor.rowcount == 1

        row = database.execute(
            """
            SELECT payload
            FROM heartbeats
            WHERE device_id=? AND message_id=?
            """,
            (device_id, message_id),
        ).fetchone()

        if row is None or row[0] != payload:
            raise ValueError(
                "heartbeat ID already exists with different content"
            )

        # 重发的心跳只确认，不重新改变告警状态。
        if inserted:
            transition = update_temperature_alarm(
                database,
                message,
                temperature,
                received_ns,
                high,
                clear,
            )

    temperature_text = (
        "unavailable"
        if temperature is None
        else f"{temperature:.2f} C"
    )

    print(
        "heartbeat saved/confirmed:",
        device_id,
        "max_thermal_zone_temperature=",
        temperature_text,
        flush=True,
    )

    if transition is not None:
        print(
            "temperature alarm:",
            device_id,
            transition,
            f"temperature={temperature:.2f} C",
            f"high={high:.2f} clear={clear:.2f}",
            flush=True,
        )

def store_board_alarm(database, message):
    if type(message.get("version")) is not int or message["version"] != 1:
        raise ValueError("invalid alarm version")

    for key in ("device_id", "message_id", "code", "stage"):
        value = message.get(key)

        if (
            not isinstance(value, str)
            or not 1 <= len(value) <= 128
            or any(
                ch not in
                "abcdefghijklmnopqrstuvwxyz"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "0123456789_-"
                for ch in value
            )
        ):
            raise ValueError("invalid alarm " + key)

    timestamp = message.get("timestamp_us")

    if type(timestamp) is not int or not 0 <= timestamp <= 9223372036854775807:
        raise ValueError("invalid alarm timestamp")

    if message.get("severity") not in ("error", "info"):
        raise ValueError("invalid alarm severity")

    payload = json.dumps(
        message,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )

    with database:
        database.execute(
            """
            INSERT OR IGNORE INTO board_alarms
            (
                device_id,
                message_id,
                received_ns,
                timestamp_us,
                code,
                stage,
                severity,
                payload
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                message["device_id"],
                message["message_id"],
                time.time_ns(),
                timestamp,
                message["code"],
                message["stage"],
                message["severity"],
                payload,
            ),
        )

        row = database.execute(
            """
            SELECT payload
            FROM board_alarms
            WHERE device_id=? AND message_id=?
            """,
            (message["device_id"], message["message_id"]),
        ).fetchone()

        if row is None or row[0] != payload:
            raise ValueError("alarm ID conflicts with stored content")

    print(
        "board alarm saved/confirmed:",
        message["device_id"],
        message["code"],
        message["stage"],
        flush=True,
    )


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        print("connected:", self.client_address, flush=True)

        # 大于客户端心跳间隔，避免空闲连接一直占用线程
        self.request.settimeout(15)

        database = None

        try:
            database = sqlite3.connect(self.server.database_path, timeout=3)
            database.execute("PRAGMA synchronous=FULL")

            while True:
                header = recv_exact(self.request, 4)
                length = struct.unpack("!I", header)[0]

                if not 1 <= length <= MAX_MESSAGE_SIZE:
                    raise ValueError("invalid message length")

                body = recv_exact(self.request, length)
                message = json.loads(
                    body.decode("utf-8"),
                    parse_constant=reject_constant,
                )

                if not isinstance(message, dict):
                    raise ValueError("message must be an object")

                if message.get("type") == "event":
                    store_event(database, message)
                    self.request.sendall(b"\x06")
                    continue
                if message.get("type") == "alarm":
                    store_board_alarm(database, message)
                    self.request.sendall(b"\x06")
                    continue

                validate_message(message)

                if message["type"] == "detection":
                    payload = json.dumps(
                        message,
                        ensure_ascii=False,
                        separators=(",", ":"),
                        allow_nan=False,
                    )

                    # 同一个设备、同一个消息 ID 只保存一次
                    database.execute(
                        """
                        INSERT OR IGNORE INTO detections
                        (device_id, message_id, received_ns, payload)
                        VALUES (?, ?, ?, ?)
                        """,
                        (
                            message["device_id"],
                            message["message_id"],
                            time.time_ns(),
                            payload,
                        ),
                    )
                    database.commit()

                    print(
                        "detection:",
                        message["device_id"],
                        message["message_id"],
                        "boxes=",
                        len(message["boxes"]),
                        flush=True,
                    )
                else:
                    store_heartbeat(
                        database,
                        message,
                        self.server.temperature_high,
                        self.server.temperature_clear,
                    )

                # 检测结果或心跳提交数据库成功后才确认。
                self.request.sendall(b"\x06")

        except (
            EOFError,
            OSError,
            ValueError,
            sqlite3.Error,
            RecursionError,
        ) as error:
            print(
                "connection closed:",
                self.client_address,
                str(error),
                flush=True,
            )
        finally:
            if database is not None:
                database.close()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--db", default="detections.sqlite3")
    parser.add_argument("--temperature-high", type=float, default=80.0)
    parser.add_argument("--temperature-clear", type=float, default=75.0)

    args = parser.parse_args()

    if (
        not math.isfinite(args.temperature_high)
        or not math.isfinite(args.temperature_clear)
        or not -100 <= args.temperature_clear < args.temperature_high <= 250
    ):
        parser.error(
            "temperature thresholds require "
            "-100 <= clear < high <= 250"
        )

    with sqlite3.connect(args.db) as database:
        database.execute("PRAGMA journal_mode=WAL")
        database.execute(
            """
            CREATE TABLE IF NOT EXISTS temperature_alarm_state (
                device_id TEXT PRIMARY KEY,
                active INTEGER NOT NULL CHECK(active IN (0, 1)),
                temperature_c REAL NOT NULL,
                updated_ns INTEGER NOT NULL
            )
            """
        )

        database.execute(
            """
            CREATE TABLE IF NOT EXISTS temperature_alarm_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                message_id TEXT NOT NULL,
                received_ns INTEGER NOT NULL,
                timestamp_us INTEGER,
                transition TEXT NOT NULL
                    CHECK(transition IN ('raised', 'cleared')),
                temperature_c REAL NOT NULL,
                high_threshold_c REAL NOT NULL,
                clear_threshold_c REAL NOT NULL,
                UNIQUE(device_id, message_id)
            )
            """
        )
        database.execute(
            """
            CREATE TABLE IF NOT EXISTS detections (
                device_id TEXT NOT NULL,
                message_id TEXT NOT NULL,
                received_ns INTEGER NOT NULL,
                payload TEXT NOT NULL,
                PRIMARY KEY (device_id, message_id)
            )
            """
        )
        database.execute(
            """
            CREATE TABLE IF NOT EXISTS events (
                device_id TEXT NOT NULL,
                event_id TEXT NOT NULL,
                received_ns INTEGER NOT NULL,
                metadata TEXT NOT NULL,
                image_sha256 TEXT NOT NULL,
                jpeg BLOB NOT NULL,
                PRIMARY KEY (device_id, event_id)
            )
            """
        )
        database.execute(
            """
            CREATE TABLE IF NOT EXISTS heartbeats (
                device_id TEXT NOT NULL,
                message_id TEXT NOT NULL,
                received_ns INTEGER NOT NULL,
                timestamp_us INTEGER,
                max_thermal_zone_temperature_c REAL,
                payload TEXT NOT NULL,
                PRIMARY KEY (device_id, message_id)
            )
            """
        )
        database.execute(
            """
            CREATE TABLE IF NOT EXISTS board_alarms (
                device_id TEXT NOT NULL,
                message_id TEXT NOT NULL,
                received_ns INTEGER NOT NULL,
                timestamp_us INTEGER NOT NULL,
                code TEXT NOT NULL,
                stage TEXT NOT NULL,
                severity TEXT NOT NULL,
                payload TEXT NOT NULL,
                PRIMARY KEY(device_id, message_id)
            )
            """
        )
        database.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_heartbeats_device_received
            ON heartbeats (device_id, received_ns DESC)
            """
        )        
               

    with Server((args.host, args.port), Handler) as server:
        server.database_path = args.db
        server.temperature_high = args.temperature_high
        server.temperature_clear = args.temperature_clear

        print(
            f"listening on {args.host}:{args.port}, db={args.db}",
            flush=True,
        )

        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("server stopped", flush=True)


if __name__ == "__main__":
    main()