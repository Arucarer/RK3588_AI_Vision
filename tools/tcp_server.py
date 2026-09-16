#!/usr/bin/env python3

import argparse
import json
import socket
import socketserver
import sqlite3
import struct
import time

MAX_MESSAGE_SIZE = 65536


def recv_exact(sock, size):
    data = bytearray()

    while len(data) < size:
        block = sock.recv(size - len(data))

        if not block:
            raise EOFError("peer disconnected")

        data.extend(block)

    return bytes(data)


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
                    print(
                        "heartbeat:",
                        message["device_id"],
                        flush=True,
                    )

                # 检测结果提交数据库成功后才确认
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
    args = parser.parse_args()

    with sqlite3.connect(args.db) as database:
        database.execute("PRAGMA journal_mode=WAL")
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

    with Server((args.host, args.port), Handler) as server:
        server.database_path = args.db

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