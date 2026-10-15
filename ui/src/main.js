import { status } from "./api.js";

const result = await status();
const profileName = result.state === "error" ? "" : result.profileName;
document.querySelector("#profile-name").textContent = profileName;
document.querySelector("#status").textContent = result.state;
