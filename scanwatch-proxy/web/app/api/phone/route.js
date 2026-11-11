import { requireIdentity } from "../../../cursor/identity";
import { setPhone } from "../../../lib/store";

export const dynamic = "force-dynamic";

export async function POST(request) {
  const identity = await requireIdentity(request);
  if (identity instanceof Response) return identity;
  let body = {};
  try {
    body = await request.json();
  } catch {
    body = {};
  }
  try {
    const watchConnected = Boolean(body.watch_connected);
    const result = await setPhone(body.floor, watchConnected);
    return Response.json(result, {
      headers: { "cache-control": "private, no-store" },
    });
  } catch (error) {
    return Response.json(
      { error: error.message || "Could not update the phone" },
      { status: 400, headers: { "cache-control": "private, no-store" } },
    );
  }
}
