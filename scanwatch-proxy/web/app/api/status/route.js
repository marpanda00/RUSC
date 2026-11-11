import { requireIdentity } from "../../../cursor/identity";
import { getState } from "../../../lib/store";

export const dynamic = "force-dynamic";

export async function GET(request) {
  const identity = await requireIdentity(request);
  if (identity instanceof Response) return identity;
  return Response.json(await getState(), {
    headers: { "cache-control": "private, no-store" },
  });
}
