modupdater
===================

**modupdater** is a tool that allows to automatically update outdated plugins (and other files). By running **modupdater.exe** you can update mods at any time,  **modupdater.asi** will do that as soon as you launch the game and it will show a dialog notification after it's done searching for all updates (do note that sometimes it takes a while).

>With **"Update all downloaded files"** checkbox, updater will replace **all** downloaded files from the archive (except asi loader, it's updated separately).
>If this checkbox is not set, only **one** file will be updated from that downloaded archive (.asi by default).
>You can also choose between 3 different methods of replacing ini files: **replace if sections or keys don't match**, **replace all** and **don't replace**.

----------

[To update different mods, settings need to be specified, see tutorials section](#tutorials).

----------


![](http://i.imgur.com/Jez2DGA.png)

----------

###  Known Issues

On first launch the updater may offer "update" to already up-to-date version of a mod. This happens because updater doesn't use any versioning information, but instead relies on date difference between local file and remote archive. Just update everything once and this problem shouldn't appear anymore.

-------------------------------

###  Installation

Put **modupdater.asi** and/or **modupdater.exe** in the folder with files you want to update. Updater will scan this folder for specified files ([see options section](#options)) and all subfolders. If you want updater to update all mods from all folders, set **ScanFromRootFolder** option to **1**.

-------------------------------

###  Options

By default updater can work without ini file, it's preconfigured to only update plugins from [Widescreen Fixes Pack](https://thirteenag.github.io/wfp). However, if you want to update something else, you need to create an ini file with the same name. For example: **modupdater.asi** / **modupdater.ini**.

> See ini example [here](https://github.com/ThirteenAG/modupdater/tree/master/inisamples) or below.

Here's a list of options you can set:

    [MODS]
    File names and update URLs, for example:
    GTA3.WidescreenFix.asi = https://github.com/ThirteenAG/WidescreenFixesPack/
    
    [MISC]
    SelfUpdate                <-- 1 or 0
    SkipUpdateCompleteDialog  <-- 1 or 0
    ScanFromRootFolder        <-- 1 or 0
    OutputLogToFile           <-- 1 or 0
    WebUrl                    <-- 1 or 0
    
    [DEBUG]
    AlwaysUpdate              <-- 1 or 0


**[MISC]**

**SelfUpdate**. Updater will update itself.

**ScanFromRootFolder**. You can place **modupdater.asi** in any folder, like scripts, modloader's subfolder etc. With this option set to **1**, updater will start searching for files from the folder where the game's exe located. This will not work if you run **modupdater.exe**.

**SkipUpdateCompleteDialog**. Skips [this](http://i.imgur.com/mM30zCH.png) dialog window(but doesn't do restart).

**OutputLogToFile**. Simply creates a log file and writes all messages to it.

**[DEBUG]**

**AlwaysUpdate**. For testing purposes, update dialog will be shown even if there's no updates available. 

See ini example [here](https://github.com/ThirteenAG/modupdater/tree/master/inisamples).

-------------------------------

###  Tutorials

I'm going to use a few random games just to demonstrate how to set everything up.

#### Need for Speed Underground

I have a copy of NFSU with an old version of widescreen fix from 2015, it's in scripts folder:

![](http://i.imgur.com/lMSyXDS.png)

I put **modupdater.asi** there as well. This is what happens after I start the game:

![](http://i.imgur.com/lCmA2SO.png)

If you click on **[ ] Update all downloaded files** checkbox, updater will replace all downloaded files from [the archive](https://github.com/ThirteenAG/WidescreenFixesPack/releases/download/nfsu/NFSUnderground.WidescreenFix.zip). If this checkbox is not set, only **NFSUnderground.WidescreenFix.asi** will be updated. Since 2015, widescreen fix for NFSU was updated several times and now includes some additional files, so I'm going to set the checkbox and download the updates ([demo](http://i.imgur.com/Okyh5bb.gifv)).

![](http://i.imgur.com/kig9kpF.png)

Scripts folder now contains the following files:

![](http://i.imgur.com/yMRXW8F.png)

**NFSUnderground.WidescreenFix.asi.deleteonnextlaunch** will be deleted after you press restart, or the next time you launch the game. Continue button closes the updater and brings the game on top, so you don't have to restart right away.

------------------------------------------

#### GTA3:

[Tutorial for GTA3 is moved to wiki.](https://github.com/ThirteenAG/modupdater/wiki/GTA3-Tutorial)
