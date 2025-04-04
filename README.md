# Spout2OverlayHUD

🎯 **Spout2OverlayHUD** is a Windows application that displays a Spout2 sender signal as a HUD overlay, with direct access to OpenGL textures. Perfect for integrating OBS scenes and sources directly on your screen!

---

## ✨ Features

✅ **Low latency**: The signal is rendered as an OpenGL texture with minimal performance impact.  
✅ **Clickthrough window**: Does not interfere with user input.  
✅ **Versatile usage**: Ideal for streamers with a single monitor, allowing real-time display of effects, chat, and interactive overlays (Crowd Control, Stream Stickers, Viewer Attack) directly in the game.

📺 **Example usage**: Use OBS to overlay webcams, browsers, monitors—the possibilities are endless!

---

## 🛠️ Technologies Used

🔹 **[OpenFrameworks](https://openframeworks.cc/)**: For window management *(Version 13 - 20250331 release used for compilation)*  
🔹 **[Spout2 SDK](https://spout.zeal.co/)**: For Spout2 signal management and rendering  

---

## 🚀 Installation & Compilation Guide

1️⃣ **Download and unpack** OpenFrameworks (13+)  
2️⃣ **Download and unpack** SpoutGL inside the `apps` folder of OpenFrameworks  
3️⃣ **Download and unpack** this repository inside the `SpoutGL` folder  

Your folder structure should look like this:

```
OpenFrameworks/
 ├── apps/
 │   ├── SpoutGL/
 │   │   ├── Spout2OverlayHUD/
 │   │   │   ├── Spout2OverlayHUD.sln
```

4️⃣ **Open `Spout2OverlayHUD.sln` with Visual Studio 2022**  
5️⃣ **Rebuild everything** and run the project! 🚀

---

## 📝 TODO

- [ ] Code optimization  
- [ ] Remove unused code  
- [ ] Fix occasional loss of "topmost" window position on startup  

---

## 🔗 Recommended Projects

Enhance your streaming experience by using these tools alongside Spout2OverlayHUD:

- 🎥 [VDO.Ninja](https://vdo.ninja/) - Webcam overlay toggleable via Stream Deck or hotkeys  
- 💬 [Social Stream Ninja](https://socialstream.ninja/) - Customizable chat overlay for any platform  
- 🎭 [Stream Stickers](https://streamstickers.com/) - Interactive stickers overlay  
- 🎮 [Viewer Attack](https://www.viewerattack.com/) - Real-time throwable stickers and effects  
- 🕹️ [Crowd Control](https://crowdcontrol.live/) - Viewer-controlled game interactions  

---

## ❤️ Support the Project

If you find Spout2OverlayHUD useful, consider supporting me with a donation! 🚀 Every contribution helps improve and maintain the project.

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/marsic1)

Thank you for your support! ✨
