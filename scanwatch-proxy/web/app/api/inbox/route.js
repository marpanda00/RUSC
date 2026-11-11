import { getState } from "../../../lib/store";
import { proxyAuthorized } from "../../../lib/proxyAuth";

export const dynamic = "force-dynamic";

export async function GET(request) {
  if (!proxyAuthorized(request)) {
    return Response.json(
      { error: "proxy token required" },
      { status: 401, headers: { "cache-control": "private, no-store" } },
    );
  }
  return Response.json(await getState(), {
    headers: { "cache-control": "private, no-store" },
  });
}
