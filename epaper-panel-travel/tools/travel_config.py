"""Shared config for epaper-panel-travel data tools.

Single source of truth for the stop, the three lines we care about, and the
data-source endpoints. Both ``imhd_realtime.py`` and ``gtfs_next_hour.py``
import from here so the filters stay in one place.
"""
from pathlib import Path

# ---------------------------------------------------------------- paths
ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
GTFS_ZIP = DATA / "idsbk_gtfs.zip"

# ---------------------------------------------------------------- stop
# imhd.sk stop id for "Karpatská" (Bratislava, sekcia "ba").
#   source: https://imhd.sk/ba/zastavka/Karpatsk%C3%A1/b6808188
#   -> "Najbližšie odchody" links to online-zastavkova-tabula?st=129&ssd=1
IMHD_STOP_ID = 129

# Karpatská has 4 nástupíst (platforms). imhd nástupiste id  <->  GTFS sub-stop:
#   nastupiste 312 <-> GTFS stop ...14400001
#   nastupiste 313 <-> GTFS stop ...14400002
#   nastupiste 314 <-> GTFS stop ...14400003
#   nastupiste 315 <-> GTFS stop ...14400004
#   i.e. nastupiste = 311 + (last GTFS sub-stop digit)
GTFS_STOP_CODE = "144"          # GTFS stop_code == "Karpatská"
IMHD_NASTUPISTE_BASE = 311      # nastupiste = BASE + sub_stop_digit
KARPA_SUB_STOPS = ("1", "2", "3", "4")   # GTFS stop_ids 00000001440000x

# ---------------------------------------------------------------- lines
# The three lines the panel shows. 32/49 leave Karpatská from nástupiste 313
# (GTFS sub-stop 2, direction_id 1); 44 leaves from nástupiste 315 (sub-stop 4,
# direction_id 0).
TARGETS = {
    "32": {
        "label": "32 → Dlhé diely (cez Hlavnú stanicu)",
        # imhd live feed: destination (cielStr / konecnaZstr) contains this
        "imhd_dest": "Dlhé diely",
        # GTFS trip_headsign contains any of these
        "gtfs_headsign": ("Dlhé diely",),
        "direction_id": "1",
        "sub_stop": "2",
    },
    "49": {
        "label": "49 → Kramáre (NÚSCH / NOÚ)",
        "imhd_dest": "Kramáre",
        "gtfs_headsign": ("NÚSCH", "Nár. onkol. ústav"),
        "direction_id": "1",
        "sub_stop": "2",
    },
    "44": {
        "label": "44 → Koliba (cez Hrad)",
        "imhd_dest": "Koliba",
        "gtfs_headsign": ("Koliba", "Depo Hroboňova"),
        "direction_id": "0",
        "sub_stop": "4",
    },
}

# ---------------------------------------------------------------- sources
# Real-time (live next departures). Socket.IO v4 (engine.io v4) over HTTP
# long-polling. This is what imhd.sk's "virtual stop display" uses.
IMHD_RT_BASE = "https://imhd.sk/rt/sio2/"

# Static timetable (GTFS). IDS BK open data (Bratislava city transit).
#   https://www.idsbk.sk/system/open-data/
# "Cestovné poriadky liniek mestskej hromadnej dopravy v Bratislave [GTFS]"
GTFS_URL = "https://www.arcgis.com/sharing/rest/content/items/aba12fd2cbac4843bc7406151bc66106/data"
# Regional bus GTFS/JDF (not needed for lines 32/49; for completeness):
GTFS_REGIONAL_URL = "https://drive.google.com/drive/folders/1n9r_hGe-msl3bGa0q9vqI_ENODHT6n7x?usp=share_link"

USER_AGENT = (
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:69.0) "
    "Gecko/20100101 Firefox/69.0"
)
