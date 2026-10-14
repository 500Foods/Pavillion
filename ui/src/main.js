import { status } from "./api.js";

const result = await status();
document.querySelector("#profile-name").textContent = result.profileName;
document.querySelector("#status").textContent = result.state;
