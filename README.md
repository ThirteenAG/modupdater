README
===================

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

At first launch updater can offer to update already latest version of the mod. It can happen because updater doesn't use any versioning information, but instead relies on date difference between local file and remote archive. Just update everything once and this problem shouldn't appear anymore.

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
**ScanFromRootFolder**. You can place **modupdater.asi** in any folder, like scripts, modloader's subfolder etc. With this option set to **1**, updater will start searching for files from the folder where the game's exe located. *This will not work if you run **modupdater.exe***.

>**UpdateUAL**. If you have Universal Asi Loader, it will be updated as well. *This will not work if you run **modupdater.exe***.

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

Here I'm going to show you how to set up a clean installation of GTA3 with some mods.

![](http://i.imgur.com/MYNA79h.png)

Download [widescreen fix](http://thirteenag.github.io/wfp#gta3), it contains asi loader.
Download [modloader](https://github.com/thelink2012/modloader/releases/latest).
Install it as usual. Then I usually move all modloader files(modloader.asi, Readme.txt, Leia-me.txt) from root to modloader folder. Also widescreen fix can be moved inside modloader folder as well.
Download more mods and install it to modloader folder. In this case, I'm going to use the following:
[SaveLoader](https://github.com/ThirteenAG/III.VC.SA.SaveLoader/releases/latest)
[WindowedMode](https://github.com/ThirteenAG/III.VC.SA.WindowedMode/releases/latest)
[SilentPatch](http://gtaforums.com/topic/669045-silentpatch/)
[SkyGfx](http://gtaforums.com/topic/750681-skygfx-ps2-and-xbox-graphics-for-pc/)

When everything is installed, file structure inside modloader folder should be something like this:

    |   Leia-me.txt
    |   modloader.asi
    |   modloader.ini
    |   modloader.log
    |   Readme.txt
    |   
    +---GTA3.WidescreenFix
    |       GTA3.WidescreenFix.asi
    |       GTA3.WidescreenFix.ini
    |       
    +---III.VC.SA.SaveLoader
    |       III.VC.SA.SaveLoader.asi
    |       III.VC.SA.SaveLoader.ini
    |       
    +---III.VC.SA.WindowedMode
    |       III.VC.SA.CoordsManager.exe
    |       III.VC.SA.WindowedMode.asi
    |       
    +---SilentPatchIII
    |       SilentPatchIII.asi
    |       
    \---skygfx_III_VC
        |   d3d8.dll
        |   README.txt
        |   rwd3d9.dll
        |   skygfx.asi
        |   
        +---III
        |   |   skygfx.ini
        |   |   
        |   \---neo
        |           carTweakingTable.dat
        |           neo.txd
        |           rimTweakingTable.dat
        |           worldTweakingTable.dat
        |           
        \---VC
            |   skygfx.ini
            |   
            \---neo
                    carTweakingTable.dat
                    neo.txd
                    rimTweakingTable.dat
                    worldTweakingTable.dat

Root folder is clean and serene.
![](http://i.imgur.com/qEQAFzM.png)

-------------------------------
Now it is time to [download the updater](https://github.com/ThirteenAG/modupdater/releases).
Extract **modupdater.asi**, **modupdater.ini** and/or **modupdater.exe** inside **scripts** folder. To be able to update all .asi files inside modloader folder, open **modupdater.ini** and set `NameRegExp = ".*"` .  `Extension = "asi"` is already set by default. Also set **ScanFromRootFolder = 1**, because scripts folder doesn't have any mods.
[Ultimate ASI Loader v2.6](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) can load **modupdater.asi** from modloader folder as well. I you have UAL v2.6 or later, you can put modupdater.asi/modupdater.ini to modloader folder instead of scripts. In that case you don't have to use **ScanFromRootFolder** option.

Modloader has 5 mods and it is also possible to update modloader itself. So I need to set up 6 different sources for mods in **modupdater.ini** (Url, Url1, Url2, Url3, Url4, Url5 parameters under API section).

 1. For widescreen fix I don't have to do anything, it's already preconfigured, Url contains the following string: `"https://api.github.com/repos/ThirteenAG/WidescreenFixesPack/releases"`
 2. For ModLoader: 
`"https://api.github.com/repos/thelink2012/modloader/releases"`
 3. For SaveLoader:
`"https://api.github.com/repos/ThirteenAG/III.VC.SA.SaveLoader/releases"`
 4. For WindowedMode:
`"https://api.github.com/repos/ThirteenAG/III.VC.SA.WindowedMode/releases"`
 5. For SilentPatch:
`"https://node-js-geturl.herokuapp.com/?url=(https://www.gtagarage.com/mods/show.php?id=25368)&selector=(tr.modrow%20%3Etd:contains(SilentPatchIII)%20~%20td%20%3E%20a)"`
 6. For SkyGfx:
`"https://node-js-geturl.herokuapp.com/?url=(http://gtaforums.com/topic/750681-skygfx-ps2-and-xbox-graphics-for-pc/)&selector=(a[href*=SkyGfx_III_VC])"`

At this point, SilentPatch and SkyGfx don't use github releases, so in order to acquire a direct link of the mod, I made a [small service for that](https://github.com/ThirteenAG/node-js-geturl#usage).

Final ini:

    [FILE]
    NameRegExp = ".*"
    Extension = "asi"
    WebUrl = "https://thirteenag.github.io/wfp"
    
    [API]
    Url = "https://api.github.com/repos/ThirteenAG/WidescreenFixesPack/releases"
    Url1 = "https://api.github.com/repos/thelink2012/modloader/releases"
    Url2 = "https://api.github.com/repos/ThirteenAG/III.VC.SA.SaveLoader/releases"
    Url3 = "https://api.github.com/repos/ThirteenAG/III.VC.SA.WindowedMode/releases"
    Url4 = "https://node-js-geturl.herokuapp.com/?url=(https://www.gtagarage.com/mods/show.php?id=25368)&selector=(tr.modrow%20%3Etd:contains(SilentPatchIII)%20~%20td%20%3E%20a)"
    Url5 = "https://node-js-geturl.herokuapp.com/?url=(http://gtaforums.com/topic/750681-skygfx-ps2-and-xbox-graphics-for-pc/)&selector=(a[href*=SkyGfx_III_VC])"
    
    [MISC]
    ScanFromRootFolder = 0
    UpdateUAL = 1
    SkipUpdateCompleteDialog = 0
    OutputLogToFile = 0
    
    [DEBUG]
    AlwaysUpdate = 0

And you're all set. In case you want to test the results, set **`AlwaysUpdate`** to **1** and launch the game. This dialog should appear:

![](http://i.imgur.com/DuiRnSD.png)
