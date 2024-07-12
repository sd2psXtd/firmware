import requests
import json
from gameDBGenerator import generateDatabase


def getGamesGameDB(system) -> dict[str, str]:
    url = f"https://github.com/niemasd/GameDB-{system}/releases/latest/download/{system}.data.json"

    r = requests.get(url, allow_redirects=True)
    games = {}

    if r.status_code == 200:
        input = json.loads(r.text)
        for id in input:
            if "redump_name" in input[id]:
                raw_name = input[id]["redump_name"]
            else:
                raw_name = input[id]["title"]
            games[id] = raw_name
     
    return games

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("dirname")
parser.add_argument("outputdir")
args = parser.parse_args()

system = ""
filename = ""
if "PS1" in args.dirname.upper():
    system = "PSX"
    filename = "gamedbps1.dat"
elif "PS2" in args.dirname.upper():
    system = "PS2"
    filename = "gamedbps2.dat"

games_dict = getGamesGameDB(system)
generateDatabase(games_dict, f"{args.outputdir}/{filename}")
