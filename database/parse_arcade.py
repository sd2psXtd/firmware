import requests
import json

def getGamesGameDB(system) -> dict[str, str]:
    url = f"https://raw.githubusercontent.com/israpps/AthenaEnv/refs/heads/aclauncher/bin/aclauncher/{system}.json"

    r = requests.get(url, allow_redirects=True)
    games = {}

    if r.status_code == 200:
        input = json.loads(r.text)
        for game in input["games"]:
            id = game["gameid"]
            if "NM" in id:
                id = int(game["gameid"].replace("NM", ""))
                if id in games:
                    common_prefix = []
                    for x, y in zip(games[id].split(), game["title"].split()):
                        if x == y:
                            common_prefix.append(x)
                        else:
                            break
                    if len(common_prefix) > 0:
                        games[id] = " ".join(common_prefix)
                    else:
                        games[id] = f"{games[id]} / {game['title']}"
                else:
                    games[id] = game["title"]


    return games

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("dirname")
parser.add_argument("outputdir")
args = parser.parse_args()

filename = "gamedbcoh.dat"

# Get games from GameDB:

games_dict = getGamesGameDB("246") | getGamesGameDB("256")


# Assemble Arcade DB:

game_ids_section_length = (len(games_dict) +1 ) * 8
game_names_section_length = 0
name_offsets = { }
with open(filename, "wb") as out:
    term = 0
    for id in games_dict:
        name_offsets[id] = game_ids_section_length + game_names_section_length
        game_names_section_length += len(games_dict[id]) + 1
        out.write(int(id).to_bytes(4, 'big'))
        out.write(name_offsets[id].to_bytes(4, 'big'))
    out.write(term.to_bytes(8, 'big'))

    for id in games_dict:
        out.write(games_dict[id].encode("ascii"))
        out.write(b"\x00")

