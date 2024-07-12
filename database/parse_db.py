import os
from gameDBGenerator import generateDatabase

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

    games_dict = {}

    # Create Mapping from serial to full game name
    for element in root:
        if element.tag == 'game':
            name, serials = parseGameEntry(element)
            for serial in serials:
                games_dict[serial] = name

    generateDatabase(games_dict, f"{outputdir}/gamedb{dirname}.dat")


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