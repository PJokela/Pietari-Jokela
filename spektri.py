import ikkunasto as ik
import numpy as np
import os
import matplotlib

#Tietorakenteet:

kirjasto = {
    "pisteet": [],
    "lineaariset_arvot": [],
    "tiedostot": [],
    "epäkelvot": [],
    "korjaus": [],
    "kineettiset_energiat": [],
    "intensiteetit": [],
    "kuvaaja": []
}


#Apufunktiot:

def piirtaja():
    """Toimii pääohjelman kutsumana sijaisfunktiona, jotta funktiota ei kutsuta, ennen napin
    painamista"""
    funktio = piirturi(
        kirjasto["kineettiset_energiat"], kirjasto["intensiteetit"])

def tyhjentaja():
    """Poistaa aiemmin valitun datan, kansion vaihdon yhteydessä"""
    for hakusana in kirjasto:
        kirjasto[hakusana] = []

def valitse_datapiste(MouseEvent):
    """Hiiren käsittelijäfunktio. Tallentaa valitut pisteet eri tietorakenteisiin,
    joista niitä kutsutaan tarvittaessa"""
    arvot = (MouseEvent.xdata)
    lineaarinen_arvo = (MouseEvent.xdata, MouseEvent.ydata)
    kirjasto["pisteet"].append(arvot)
    kirjasto["lineaariset_arvot"].append(lineaarinen_arvo)

def ikkuna(nimi, x_data, y_data, syote, funktio):
    """Koodissa toistuva ikkunanluontifunktio, joka kutsuttaessa tekee halutunlaisen
    ikkunan halutuilla funktioilla"""
    nimi = ik.luo_ali_ikkuna("Spektri")
    kirjasto[nimi] = nimi
    piirtoalue, kuvaaja = ik.luo_kuvaaja(nimi, valitse_datapiste, 1000, 650)
    kirjasto["kuvaaja"] = kuvaaja
    lisaa = kuvaaja.add_subplot()
    lisaa.plot(x_data, y_data)
    lisaa.set_xlabel('Energia')
    lisaa.set_ylabel('Intensiteetti')
    piirtoalue.draw()
    ik.luo_nappi(nimi, syote, funktio)
    ik.luo_nappi(nimi, "Tallenna", tallentaja)
    kirjasto["pisteet"] = []
    ik.kaynnista()

def syote(virherivit):
    if virherivit:
        ik.avaa_viesti_ikkuna(
            "Tiedostoja",
            "Läpikäytyjä kelvollisia tiedostoja: {i}. \
            Virheellisiä tiedostoja {j} ja virheellisiä rivejä tiedostoissa {k}".format(
            i=len(kirjasto["tiedostot"]), j=kirjasto["epäkelvot"], k=virherivit
                ))
    else:
        ik.avaa_viesti_ikkuna(
            "Tiedostoja",
            "Läpikäytyjä kelvollisia tiedostoja: {i}. \
            Virheellisiä tiedostoja {j} ".format(
            i=len(kirjasto["tiedostot"]), j=kirjasto["epäkelvot"]
                ))

def arkistoija(energiat):
    kineettinen_energia, intensiteettipiikit = zip(*energiat.items())
    kirjasto["kineettiset_energiat"].extend(list(np.float_(kineettinen_energia)))
    kirjasto["intensiteetit"].extend(list(np.float_(intensiteettipiikit)))

def lajittelija(rivi, energiat, virherivit, tiedosto):
    try:
        energia, intensiteetti = rivi.split(" ")
        float(intensiteetti)
    except ValueError:
        virherivit.append(rivi)
        virherivit.append(tiedosto)
    else:
        if energia in energiat.keys():
            energiat[energia] += float(intensiteetti)
        elif energia not in energiat.keys():
            energiat[energia] = float(intensiteetti)

def etsi_indeksit(lista, minimiarvo, maksimiarvo):
    indeksit = []
    for i, arvo in enumerate(lista):
        if arvo >= minimiarvo and arvo <= maksimiarvo:
            indeksit.append(i)
    return indeksit[0], indeksit[-1] + 1


#Varsinaiset funktiot:

def datalaturi():
    """Avaa hakemistoikkunan, jonka avulla käyttäjä valitsee haluamansa datakansion.
    Jos käyttäjä avaa toiminnon uudestaan pyyhkiytyy aiemmin valittu data pois.
    Mahdollistaa siis vain yhden kansion käytön kerrallaan"""
    tyhjentaja()
    try:
        energiat = {}
        virherivit = []
        kansio = ik.avaa_hakemistoikkuna("Datahakemisto")
        tiedostot = os.listdir(kansio)
        for tiedosto in tiedostot:
            polku = os.path.join(kansio, tiedosto)
            if polku.endswith("txt"):
                kirjasto["tiedostot"].append(tiedosto)
                with open(polku) as lahde:
                    for rivi in lahde:
                        lajittelija(rivi, energiat, virherivit, tiedosto)
            elif not polku.endswith("txt"):
                kirjasto["epäkelvot"].append(tiedosto)
        arkistoija(energiat)
        syote(virherivit)
    except FileNotFoundError:
        ik.avaa_viesti_ikkuna("Error", "Valittuja tiedostoja ei löytynyt. Ole hyvä ja valitse kansio")
    except ValueError:
        ik.avaa_viesti_ikkuna("Error", "Valittuja tiedostoja ei löytynyt. Ole hyvä ja valitse kansio")

def piirturi(x_data, y_data):
    """Piirtää tallennetun datan mukaisen spektrin"""
    if x_data:
        ikkuna("spektrikehys", x_data, y_data, "Poista lineaarinen tausta", lineaarinen)
    elif not x_data or not y_data:
        ik.avaa_viesti_ikkuna("Error", "Dataa ei ladattu, joten spektrin piirto epäonnistui")

def integrointi():
    """Integroi kuvaajalta valittujen pisteiden perusteella intensiteetin"""
    if kirjasto["pisteet"]:
        try:
            indeksi_1, indeksi_2 = etsi_indeksit(
                kirjasto["kineettiset_energiat"], kirjasto["pisteet"][0], kirjasto["pisteet"][1]
            )
            intesiteetti_kayra = kirjasto["korjaus"][indeksi_1:indeksi_2]
            energia_kayra = kirjasto["kineettiset_energiat"][indeksi_1:indeksi_2]
            integraali = np.trapz(intesiteetti_kayra, energia_kayra)
            ik.avaa_viesti_ikkuna("integraali", integraali)
            kirjasto["pisteet"] = []
            intesiteetti_kayra = []
            energia_kayra = []
            return
        except IndexError:
            ik.avaa_viesti_ikkuna("Error", "Integrointivälin valinnassa tapahtui virhe")
            kirjasto["pisteet"] = []
            intesiteetti_kayra = []
            energia_kayra = []
    else:
        ik.avaa_viesti_ikkuna("Error", "Integrointiväliä ei ole valittu")

def lineaarinen():
    """Poistaa lineaarisen taustan ja muuntaa spektriun luotettavaksi"""
    x = []
    y = []
    if not kirjasto["korjaus"]:
        try:
            for erottaja in kirjasto["lineaariset_arvot"]:
                x_arvo, y_arvo = erottaja
                x.append(x_arvo)
                y.append(y_arvo)
                kirjasto["lineaariset_arvot"] = []
                kirjasto["pisteet"] = []
            if x and x[0] != x[1] and y[0] != y[1]:
                kk = (y[1]-y[0])/(x[1]-x[0])
                intensiteetti_korjaus = []
                for j in kirjasto["kineettiset_energiat"]:
                    y_korjaava = kk * (j - x[0]) + y[0]
                    intensiteetti_korjaus.append(y_korjaava)
                for k, l in enumerate(kirjasto["intensiteetit"]):
                    korjaus = l - intensiteetti_korjaus[k]
                    kirjasto["korjaus"].append(korjaus)
            else:
                ik.avaa_viesti_ikkuna("Error", "Korjauspisteiden valinnassa tapahtui virhe")
                return
        except IndexError:
             ik.avaa_viesti_ikkuna("Error", "Korjauspisteitä ei ole valittu")
        else:
            ikkuna("korjattu_spektri", kirjasto["kineettiset_energiat"], kirjasto["korjaus"], "Integroi", integrointi)
    else:
        ikkuna("korjattu_spektri", kirjasto["kineettiset_energiat"], kirjasto["korjaus"], "Integroi", integrointi)
        
def tallentaja():
    sijainti = ik.avaa_tallennusikkuna("Tallenna")
    kirjasto["kuvaaja"].savefig(sijainti)


#Pääohjelma

def main():
    """Pääohjelma"""
    ikkuna = ik.luo_ikkuna("Spektrivalikko")
    nappi_kehys = ik.luo_kehys(ikkuna, ik.VASEN)
    nappi_kehys2 = ik.luo_kehys(ikkuna, ik.VASEN)
    ik.luo_nappi(nappi_kehys, "Lataa data", datalaturi)
    ik.luo_nappi(nappi_kehys, "Piirrä data muistista", piirtaja)
    ik.luo_nappi(nappi_kehys2, "Lopeta", ik.lopeta)
    ik.kaynnista()

if __name__ == "__main__":
    main()
