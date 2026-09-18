#!/bin/sh

CONFIG_FILE=/opt/rk3588_ai/config/network.conf

IFACE=eth0
IP_ADDR=192.168.88.101
PREFIX=24
GATEWAY=
DNS1=
DNS2=
LINK_WAIT_SECONDS=5

fail()
{
    echo "[ERROR] $*" >&2
    exit 1
}

if [ ! -r "$CONFIG_FILE" ]; then
    fail "network configuration missing: $CONFIG_FILE"
fi

# 此文件由管理员维护，使用 Shell 的变量赋值格式。
. "$CONFIG_FILE"

case "$IFACE" in
    ""|*[!a-zA-Z0-9_.:-]*)
        fail "invalid interface name"
        ;;
esac

case "$LINK_WAIT_SECONDS" in
    ""|*[!0-9]*)
        fail "invalid LINK_WAIT_SECONDS"
        ;;
esac

if [ "$LINK_WAIT_SECONDS" -gt 30 ]; then
    fail "LINK_WAIT_SECONDS must be 0..30"
fi

if ! ip link show dev "$IFACE" >/dev/null 2>&1; then
    fail "interface not found: $IFACE"
fi

if ! ip link set dev "$IFACE" up; then
    fail "cannot enable interface: $IFACE"
fi

# 不清空网卡已有地址，避免删除其他服务配置的 IPv4 地址。
if ! ip -4 addr replace "$IP_ADDR/$PREFIX" dev "$IFACE"; then
    fail "cannot configure address: $IP_ADDR/$PREFIX"
fi

# 只有明确配置了网关，才修改默认路由。
if [ -n "$GATEWAY" ]; then
    if ! ip -4 route replace default via "$GATEWAY" dev "$IFACE"; then
        fail "cannot configure gateway: $GATEWAY"
    fi
fi

# 没有配置 DNS 时，保留系统原来的 resolv.conf。
if [ -n "$DNS1" ] || [ -n "$DNS2" ]; then
    if ! (
        if [ -n "$DNS1" ]; then
            printf 'nameserver %s\n' "$DNS1"
        fi

        if [ -n "$DNS2" ]; then
            printf 'nameserver %s\n' "$DNS2"
        fi

        :
    ) > /etc/resolv.conf; then
        fail "cannot write /etc/resolv.conf"
    fi
fi

waited=0
carrier=0

while :; do
    carrier=$(cat "/sys/class/net/$IFACE/carrier" 2>/dev/null)

    if [ "$carrier" = "1" ]; then
        break
    fi

    if [ "$waited" -ge "$LINK_WAIT_SECONDS" ]; then
        break
    fi

    sleep 1
    waited=$((waited + 1))
done

echo "[INFO] network interface=$IFACE address=$IP_ADDR/$PREFIX"

if [ -n "$GATEWAY" ]; then
    echo "[INFO] gateway=$GATEWAY"
fi

if [ "$carrier" != "1" ]; then
    echo "[WARN] physical link is down; local AI detection can still start"
else
    echo "[INFO] physical link is ready"
fi

exit 0