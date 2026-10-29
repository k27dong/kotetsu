import { onSignedOut } from "./api";
import { showSignIn, startRouter } from "./router";

// there is no session probe: the first page's own request answers 401 when signed out
onSignedOut(showSignIn);
startRouter();
