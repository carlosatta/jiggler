import asyncio, sys
from bleak import BleakScanner, BleakClient
CHAR = "0000fff1-0000-1000-8000-00805f9b34fb"
want = sys.argv[1]
async def main():
    found = {}
    def cb(dev, adv):
        if (adv.local_name or dev.name or "") == want: found[dev.address] = dev
    async with BleakScanner(cb):
        await asyncio.sleep(6)
    if not found: print(want, "not seen"); return
    dev = list(found.values())[0]
    c = BleakClient(dev)
    try:
        await asyncio.wait_for(c.connect(), 25)
        v = await c.read_gatt_char(CHAR)
        print(want, "connected, read", v)
        await asyncio.sleep(5)
        print(want, "still connected:", c.is_connected)
    except Exception as e:
        print(want, "CONNECT/READ FAILED:", type(e).__name__, e)
    finally:
        try: await c.disconnect()
        except Exception: pass
asyncio.run(main())
