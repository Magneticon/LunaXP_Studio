# LunaXP Studio
Comprehensive Luna Theme Designer for Windows XP / XP x64

Allows you to easily edit Luna theme styles for Windows XP / XP x64. You can easily recolor the Luna theme, expanding the Luna options beyond stock Blue, Homestead and Metallic themes.

Moreover, a dark mode support for Luna theme was implemented, bringing a dark mode to Windows XP. Applications supporting dark mode can detect a dark mode setting on Windows XP and set their theme accordingly - the detection can be either thru registry values or you can supply LunaDarkMode.dll to your application directory to load missing entries for dark mode calls, which are missing in stock Windows XP.

Note: Such setup might not work with all programs claiming to support dark mode. Some programs might directly load respective procedures from Windows system DLLs. These DLLs are not in such old operating systems as Windows XP and porting whole Windows 10+ Windowing/GDI manager would be too complicated and the effort wouldn't be worth it.

This solution is provided AS IS. Use on your own risk. Test in Virtual Machine first. There are bugs in it.

Moreover, you will need patched uxtheme.dll, as you need it for any Luna theme modifications on both, Windows XP & XP x64. For Windows XP x64 uxtheme.dll patching, you need to patch only the one in system32 directory, the WOW64 version does not need patching.

To manually patch the XP x64 uxtheme.dll, in binary editor of your choice edit the following: at offset 0x150 change 50 94 to A5 56 and at offset 0x12DC3 change 48 83 EC 78 to 33 C0 C3 90.

<img width="1265" height="614" alt="lunas1" src="https://github.com/user-attachments/assets/0fedcf32-6acf-43c6-9eb9-c014254abd18" />
<img width="1920" height="1080" alt="lunas2" src="https://github.com/user-attachments/assets/a5622069-0d3b-41f1-a962-3c831c412e8a" />
<img width="1920" height="1080" alt="lunas3" src="https://github.com/user-attachments/assets/87bae93b-3b5d-4a6d-b933-b703c0d3222a" />

Example of Dark theme aware application on Windows XP, loading system dark theme settings automatically - Supermium Browser v144:

<img width="1920" height="1080" alt="lunas4" src="https://github.com/user-attachments/assets/d207b828-e652-474b-9191-01cd02936a06" />
<img width="1920" height="1080" alt="lunas5" src="https://github.com/user-attachments/assets/d7d314f9-70d6-4875-b719-66cf087a27bb" />
<img width="1920" height="1080" alt="lunas6" src="https://github.com/user-attachments/assets/826d123b-1909-49ed-9b25-3207cf267e58" />
<img width="1920" height="1080" alt="lunas7" src="https://github.com/user-attachments/assets/7dadd3b1-5fb2-46e9-a233-f7a645eaf5f0" />
