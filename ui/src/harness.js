import { createApi } from "./api.js";
import { mount } from "./index.js";
import { mockTransport } from "./transport/mock.js";

const api = createApi(mockTransport());
await mount(document.querySelector("#box"), api, {});
