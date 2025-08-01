import sys
import json
import re
from unidecode import unidecode


disc_pattern = r'\(Disc (\d)\)'
replacer = r'\((.*)\)'

class GameId:
    name = ""
    id = ""
    prefix = ""
    parent_id = ""
    def __init__(self, name, id, parent_id=None):
        self.name = unidecode(name)
        separator = "_"
        if "-" in id:
            separator = "-"
        self.id = id.split(separator)[1].replace(".", "")
        self.prefix = id.split(separator)[0]
        if parent_id:
            self.parent_id = parent_id.split(separator)[1]
        else:
            self.parent_id = self.id
    def __str__(self):
        return "Prefix " + self.prefix + " Id " +  self.id + " Name " + self.name + " Parent " + self.parent_id

    def __lt__(self, o):
        return self.name < o.name


def writeSortedGameList(outfile, prefixes, games_count, games_sorted, gamenames):
    term = 0
    # Calculate general offsets
    game_ids_offset = (len(prefixes) + 1) * 8
    game_names_base_offset = game_ids_offset + (games_count * 12) + (len(prefixes) * 12)
    prefix_offset = game_ids_offset

    offset = game_names_base_offset
    game_name_to_offset = {}
    # Calculate offset for each game name
    for gamename in gamenames:
        game_name_to_offset[gamename] = offset
        offset = offset + len(gamename) + 1
    # First: write prefix Indices in the format 
    # 4 Byte: Index Chars, padded with ws in the end
    # 4 Byte: Index Offset within dat
    for prefix in games_sorted:
        adjustedPrefix = prefix
        if len(prefix) < 4:
            adjustedPrefix = prefix + (4 - len(prefix) ) * " "
        outfile.write(adjustedPrefix.encode('ascii'))
        outfile.write(prefix_offset.to_bytes(4, 'big'))
        prefix_offset = prefix_offset + (len(games_sorted[prefix]) + 1) * 12
    outfile.write(term.to_bytes(8, 'big'))
    # Next: write game entries for each index in the format:
    # 4 Byte: Game ID without prefix, Big Endian
    # 4 Byte: Offset to game name, Big Endian
    # 4 Byte: Parent Game ID - if multi disc this is equal to Game ID
    for prefix in games_sorted:
        for game in games_sorted[prefix]:
            #print(game)
            outfile.write(int(game.id).to_bytes(4, 'big'))
            outfile.write(game_name_to_offset[game.name].to_bytes(4, 'big'))
            outfile.write(int(game.parent_id).to_bytes(4, 'big'))
        outfile.write(term.to_bytes(12, 'big'))
    # Last: write null terminated game names
    for game in game_name_to_offset:
        outfile.write(game.encode('ascii'))
        outfile.write(term.to_bytes(1, 'big'))

def getGamesGameDB() -> ([], [], {}, int):
    prefixes = []
    gamenames = []
    games_sorted = {}
    parent_serials = {}
    games_count = 0
    name_to_serials = {}

    data = ""
    with open(sys.argv[3] + "/PSX.data.json", "r") as f:
        data = f.read()


    if True:
        input = json.loads(data)
        for id in input:
            try:
                if "redump_name" in input[id]:
                    raw_name = input[id]["redump_name"]
                    clean_name = re.sub(replacer, "", input[id]["redump_name"]).strip()
                else:
                    raw_name = input[id]["title"]
                    clean_name = re.sub(replacer, "", input[id]["title"]).strip()
                parent_id = id
                game = GameId(clean_name, id)
                if len(game.prefix) > 4:
                    raise ValueError
                if int(game.id) > 0:
                    # Create Prefix list and game name list
                    # Create dict that contains all games sorted by prefix
                    if game.prefix not in prefixes:
                        prefixes.append(game.prefix)
                    if game.name not in gamenames:
                        gamenames.append(game.name)
                    if not game.prefix in games_sorted:
                        games_sorted[game.prefix] = []
                    name_to_serials[f"{game.prefix}{raw_name}"] = id
                    match = re.search(disc_pattern, raw_name)
                    if match and match[0] != "(Disc 1)":
                        parent_name = raw_name.replace(match[0], "(Disc 1)")

                        if f"{game.prefix}{parent_name}" in name_to_serials:
                            parent_id = name_to_serials[f"{game.prefix}{parent_name}"]
                            game.parent_id = parent_id.split("-")[1]
                    games_sorted[game.prefix].append(game)
                    games_count += 1
            except ValueError:
                #print(f"{game} not parsed - wrong value")
                continue
            except IndexError:
                #print(f"{game} not parsed - wrong gameid format")
                continue
     
    print(f"Parsed {games_count} games in {len(prefixes)} Prefixes")
    return (prefixes, gamenames, games_sorted, games_count)


prefixes = []
gamenames = []
games_sorted = {}
games_count = 0


with open(sys.argv[4], "wb") as out:
    (prefixes, gamenames, games_sorted, games_count) = getGamesGameDB()
    writeSortedGameList(out, prefixes, games_count, games_sorted, gamenames)

