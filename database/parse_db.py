import os

serial_pattern = r'([A-Z]{3,4}[- ]\d+)'
disc_pattern = r'\(Disc (\d)\)'
replacer = r'\((.*)\)'

class GameId:
    name = ""
    id = ""
    prefix = ""
    parent_id = ""
    def __init__(self, name, id, parent_id=None):
        self.name = name
        self.id = id.split("-")[1]
        self.prefix = id.split("-")[0]
        if parent_id:
            self.parent_id = parent_id.split("-")[1]
        else:
            self.parent_id = self.id
    def __str__(self):
        return "Prefix " + self.prefix + " Id " +  self.id + " Name " + self.name + " Parent " + self.parent_id

    def __lt__(self, o):
        return self.name < o.name


def getFileName(rootdir):
    regex = re.compile('(.*dat$)')
    filename = None

    for root, dirs, files in os.walk(rootdir):
        for file in files:
            if regex.match(file):
                filename = "{}/{}".format(root, file)
    return filename
    
def parseGameEntry(element):
    name = element.attrib["name"]
    serials = element.findall("serial")
    game_serials = []
    if serials and len(serials) > 0:
        matches = re.findall(serial_pattern, serials[0].text)
        for m in matches:
            clean_serial = m.replace(" ", "-")
            if clean_serial not in game_serials:
                game_serials.append(clean_serial)
    return (name, game_serials)

def createGameList(name_to_serials):

    gamenames_full = list(name_to_serials.keys())
    gamenames_full.sort()

    gameList = []

    # Try to figure out multi disc games by game name
    parent_serials = {}
    for game in gamenames_full:
        match = re.search(disc_pattern, game)
        if match and match[0] != "(Disc 1)":
            parent_name = game.replace(match[0], "(Disc 1)")
            if parent_name in name_to_serials:
                parent_id = name_to_serials[parent_name]
                for i in range(0, min(len(name_to_serials[parent_name]), len(name_to_serials[game]))):
                    parent_serials[name_to_serials[game][i]] = name_to_serials[parent_name][i]
        for serial in name_to_serials[game]:
            gameName = re.sub(replacer, "", game).strip()
            parent_serial = None
            if serial in parent_serials:
                parent_serial = parent_serials[serial]
            gameList.append(GameId(gameName, serial, parent_serial))
    return gameList

import xml.etree.ElementTree as ET
import re

def createDbFile(rootdir, outputdir):
    dirname = rootdir.split("/")[-1]
    if len(dirname) < 1:
        dirname = rootdir.split("/")[-2]

    tree = ET.parse(getFileName(rootdir))

    root = tree.getroot()

    name_to_serials = {}

    # Create Mapping from serial to full game name
    for element in root:
        if element.tag == 'game':
            name, serials = parseGameEntry(element)
            name_to_serials[name] = serials


    redump_games = createGameList(name_to_serials)

    prefixes = []
    gamenames = []
    games_sorted = {}


    # Create Prefix list and game name list
    # Create dict that contains all games sorted by prefix
    for game in redump_games:
        if game.prefix not in prefixes:
            prefixes.append(game.prefix)
        if game.name not in gamenames:
            gamenames.append(game.name)
        if not game.prefix in games_sorted:
            games_sorted[game.prefix] = []
        games_sorted[game.prefix].append(game)
        

    print("Redump {} Game Names".format(len(gamenames)))
    print("Redump {} Games".format(len(redump_games)))

    redump_games.sort()
    term = 0

    print("{} Prefixes".format(len(prefixes)))
    game_ids_offset = (len(prefixes) + 1) * 8
    game_names_base_offset = game_ids_offset + (len(redump_games) * 12) + (len(prefixes) * 12)
    prefix_offset = game_ids_offset

    offset = game_names_base_offset
    game_name_to_offset = {}
    # Calculate offset for each game name
    for gamename in gamenames:
        game_name_to_offset[gamename] = offset
        offset = offset + len(gamename) + 1

    with open("{}/gamedb{}.dat".format(outputdir, dirname), "wb") as out:
        # First: write prefix Indices in the format 
        # 4 Byte: Index Chars, padded with ws in the end
        # 4 Byte: Index Offset within dat
        for prefix in games_sorted:
            adjustedPrefix = prefix
            if len(prefix) < 4:
                adjustedPrefix = prefix + (4 - len(prefix) ) * " "
            out.write(adjustedPrefix.encode('ascii'))
            out.write(prefix_offset.to_bytes(4, 'big'))
            prefix_offset = prefix_offset + (len(games_sorted[prefix]) + 1) * 12
        out.write(term.to_bytes(8, 'big'))
        # Next: write game entries for each index in the format:
        # 4 Byte: Game ID without prefix, Big Endian
        # 4 Byte: Offset to game name, Big Endian
        # 4 Byte: Parent Game ID - if multi disc this is equal to Game ID
        for prefix in games_sorted:
            for game in games_sorted[prefix]:
                out.write(int(game.id).to_bytes(4, 'big'))
                out.write(game_name_to_offset[game.name].to_bytes(4, 'big'))
                out.write(int(game.parent_id).to_bytes(4, 'big'))
            out.write(term.to_bytes(12, 'big'))
        # Last: write null terminated game names
        for game in game_name_to_offset:
            out.write(game.encode('ascii'))
            out.write(term.to_bytes(1, 'big'))


from urllib.request import urlopen
from io import BytesIO
from zipfile import ZipFile


def downloadDat(path):
    if "ps1" in path:
        url = "http://redump.org/datfile/psx/serial"
    elif "ps2" in path:
        url = "http://redump.org/datfile/ps2/serial"
    http_response = urlopen(url)
    zipfile = ZipFile(BytesIO(http_response.read()))
    zipfile.extractall(path=path)

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("dirname")
parser.add_argument("outputdir")
args = parser.parse_args()

downloadDat(args.dirname)

createDbFile(args.dirname, args.outputdir)