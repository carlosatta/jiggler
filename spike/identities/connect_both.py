import asyncio
from bleak import BleakScanner, BleakClient
async def find(name, secs=6):
    found = {}
    def cb(dev, adv):
        if (adv.local_name or dev.name or "") == name: found[dev.address] = dev
    async with BleakScanner(cb): await asyncio.sleep(secs)
    return list(found.values())
async def count_ads(secs=8):
    counts = {}
    def cb(dev, adv):
        n = adv.local_name or dev.name or ""
        if n.startswith("Spike"): counts[n] = counts.get(n, 0) + 1
    async with BleakScanner(cb): await asyncio.sleep(secs)
    return counts
async def main():
    a_dev = (await find("SpikeA"))[0]
    a = BleakClient(a_dev); await asyncio.wait_for(a.connect(), 20); print("A connected")
    print("ads seen while A connected (8 s):", await count_ads())
    b_list = await find("SpikeB", 6)
    print("B visible while A connected:", bool(b_list))
    if b_list:
        b = BleakClient(b_list[0])
        try:
            await asyncio.wait_for(b.connect(), 25); print("B connected too; both:", a.is_connected, b.is_connected)
            await asyncio.sleep(8); print("after 8 s:", a.is_connected, b.is_connected)
            await b.disconnect()
        except Exception as e: print("B connect failed while A connected:", type(e).__name__, e)
    await a.disconnect()
asyncio.run(main())
