import { recordPhoneFloor } from "../../../../lib/store";
import { proxyAuthorized } from "../../../../lib/proxyAuth";

export const dynamic = "force-dynamic";

export async function POST(request) {
  if (!proxyAuthorized(request)) {
    return Response.json(
      { error: "proxy token required" },
      { status: 401, headers: { "cache-control": "private, no-store" } },
    );
  }
  let body = {};
  try {
    body = await request.json();
  } catch {
    body = {};
  }
  try {
    const state = await recordPhoneFloor(body.floor);
    return Response.json(state, {
      headers: { "cache-control": "private, no-store" },
    });
  } catch (error) {
    return Response.json(
      { error: error.message || "Could not record phone floor" },
      { status: 400, headers: { "cache-control": "private, no-store" } },
    );
  }
}