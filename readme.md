modupdater
===================

[GIFV DEMO](http://i.imgur.com/aKDwZv8.gifv)

**modupdater** is a tool that allows to automatically update outdated plugins (and other files). By running **modupdater.exe** you can update mods at any time,  **modupdater.asi** will do that as soon as you launch the game and it will show a dialog notification after it's done searching for all updates (do note that sometimes it takes a while).

>With **"Update all downloaded files"** checkbox, updater will replace **all** downloaded files from the archive (except asi loader, it's updated separately). That means, in case ini file is in the archive, it will be replaced and you'll have to configure it again if necessary.
>If this checkbox is not set, only **one** file will be updated from that downloaded archive (.asi by default).

----------

[To update different mods, settings need to be specified, see tutorials section](#tutorials).

----------


![](http://i.imgur.com/EE2sWRn.png)

----------

###  Known Issues

On first launch the updater may offer "update" to already up-to-date version of a mod. This happens because updater doesn't use any versioning information, but instead relies on date difference between local file and remote archive. Just update everything once and this problem shouldn't appear anymore.

-------------------------------

###  Installation

Put **modupdater.asi** and/or **modupdater.exe** in the folder with files you want to update. Updater will scan this folder for specified files ([see options section](#options)) and all subfolders. If you want updater to update all mods from all folders, set **ScanFromRootFolder** option to **1**. Not all options and features are supported by **modupdater.exe**, but **.asi** version works with all of them. 

-------------------------------

###  Options

By default updater can work without ini file, it's preconfigured to only update plugins from [Widescreen Fixes Pack](https://thirteenag.github.io/wfp). However, if you want to update something else, you need to create an ini file with the same name. For example: **modupdater.asi** / **modupdater.ini**.

> See ini example [here](https://github.com/ThirteenAG/modupdater/blob/master/source/Ini%20Example/) or below.

Here's a list of options you can set:

    [FILE]
    NameRegExp <-- string
    Extension  <-- string
    WebUrl     <-- string
    
    [API]
    Url        <-- string
    Url1       <-- string
    ...
    Url#       <-- string
    
    [MISC]
    ScanFromRootFolder       <-- 1 or 0
    UpdateUAL                <-- 1 or 0
    SkipUpdateCompleteDialog <-- 1 or 0
    OutputLogToFile          <-- 1 or 0
    
    [DEBUG]
    AlwaysUpdate             <-- 1 or 0


>**[FILE]**
**NameRegExp**. It should contain a regular expression, by default it's **`".*\\.WidescreenFix"`**. Set it to **`".*"`** to scan all files or to something else.

>**Extension**. Only files with specified extension will be scanned. By default it's **`"asi"`**.

>**WebUrl**. Link that will be shown at the bottom of the dialogue box. By default it's **`"https://thirteenag.github.io/wfp"`**. 

>**[API]**
**Url**. Contains a default source path for updated files. By default it's **`"https://api.github.com/repos/ThirteenAG/WidescreenFixesPack/releases"`**.

>**Url1**. Contains an additional source path. You can also specify **Url2**, **Url3**, **Url4** and so on.

>**[MISC]**
**ScanFromRootFolder**. You can place **modupdater.asi** in any folder, like scripts, modloader's subfolder etc. With this option set to **1**, updater will start searching for files from the folder where the game's exe located. This will not work if you run **modupdater.exe**.

>**UpdateUAL**. If you have Universal Asi Loader, it will be updated as well. This will not work if you run **modupdater.exe**.

>**SkipUpdateCompleteDialog**. Skips this dialog window(but doesn't do restart):
![](http://i.imgur.com/mM30zCH.png)

>**OutputLogToFile**. Simply creates a log file and writes all messages to it.

>**[DEBUG]**
**AlwaysUpdate**. For testing purposes, update dialog will be shown even if there's no updates available. 

See ini example [here](https://github.com/ThirteenAG/modupdater/blob/master/source/Ini%20Example/modupdater.ini) or below.

-------------------------------

###  Tutorials

I'm going to use a few random games just to demonstrate how to set everything up.

#### Need for Speed Underground

I have a copy of NFSU with an old version of widescreen fix from 2015, it's in scripts folder:

![](http://i.imgur.com/lMSyXDS.png)

I put **modupdater.asi** there as well. This is what happens after I start the game:

![](http://i.imgur.com/gbjqfx2.png)

If you click on **[ ] Update all downloaded files** checkbox, updater will replace all downloaded files from [the archive](https://github.com/ThirteenAG/WidescreenFixesPack/releases/download/nfsu/NFSUnderground.WidescreenFix.zip)(except asi loader, it's updated separately). If this checkbox is not set, only **NFSUnderground.WidescreenFix.asi** will be updated. Since 2015, widescreen fix for NFSU was updated several times and now includes some additional files, so I'm going to set the checkbox and download the updates ([demo](http://i.imgur.com/dmxYPGQ.gifv)).

![](http://i.imgur.com/LCLBtBf.png)

Scripts folder now contains the following files:

![](http://i.imgur.com/yMRXW8F.png)

**NFSUnderground.WidescreenFix.asi.deleteonnextlaunch** will be deleted after you press restart, or the next time you launch the game. Continue button closes the updater and brings the game on top, so you don't have to restart right away.

------------------------------------------

####GTA3:

[Tutorial for GTA3 is moved to wiki.](https://github.com/ThirteenAG/modupdater/wiki/GTA3-Tutorial)
