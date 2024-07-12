import requests
import csv
from unidecode import unidecode
from gameDBGenerator import generateDatabase

def getGamesHDLBatchInstaller() -> dict[str, str]:

    games_dict = {}

    url = "https://github.com/israpps/HDL-Batch-installer/raw/main/Database/gamename.csv"

    r = requests.get(url, allow_redirects=True)


    if r.status_code == 200:
        lines = r.text.split("\n")
        csv_reader = csv.reader(lines, delimiter=";")
        for row in csv_reader:
            if len(row) == 2:
                id = row[0]
                title = row[1]
                games_dict[id] = title
    return games_dict



import argparse
parser = argparse.ArgumentParser()
parser.add_argument("dirname")
parser.add_argument("outputdir")
args = parser.parse_args()

filename = ""
if "PS1" in args.dirname.upper():
    raise ValueError("PS1 not available")
elif "PS2" in args.dirname.upper():
    filename = "gamedbps2.dat"

games_dict = getGamesHDLBatchInstaller()
generateDatabase(games_dict, f"{args.outputdir}/{filename}")
